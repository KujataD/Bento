#include "CanvasComponent.h"

#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"
#include "../scene/UnityActionUI.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace KujataEngine {
namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

Vector2 ReadVector2(const nlohmann::json& json, const char* key, const Vector2& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) {
		return defaultValue;
	}
	return {value[0].get<float>(), value[1].get<float>()};
}

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

} // namespace

CanvasComponent::Layout CanvasComponent::GetLayout(float targetWidth, float targetHeight) const {
	Layout layout;

	// World Spaceは画面解像度に依存しない。キャンバス単位のサイズがそのままローカル座標系になり、
	// world単位への変換はGameObjectのTransform(scale)が担う。
	if (renderMode_ == RenderMode::WorldSpace) {
		layout.scaleFactor = 1.0f;
		layout.canvasWidth = (std::max)(worldCanvasSize_.x, 0.0001f);
		layout.canvasHeight = (std::max)(worldCanvasSize_.y, 0.0001f);
		return layout;
	}

	float scale = 1.0f;
	if (scaleWithScreenSize_ && referenceResolution_.x > 0.0f && referenceResolution_.y > 0.0f && targetWidth > 0.0f && targetHeight > 0.0f) {
		const float logWidth = std::log2(targetWidth / referenceResolution_.x);
		const float logHeight = std::log2(targetHeight / referenceResolution_.y);
		const float lerped = logWidth + (logHeight - logWidth) * matchWidthHeight_;
		scale = std::pow(2.0f, lerped);
	}
	if (scale < 0.0001f) {
		scale = 0.0001f;
	}
	layout.scaleFactor = scale;
	layout.canvasWidth = targetWidth / scale;
	layout.canvasHeight = targetHeight / scale;
	return layout;
}

void CanvasComponent::DrawInspector() {
#ifdef USE_IMGUI
	const char* renderModeItems[] = {"Screen Space - Overlay", "World Space"};
	int renderModeIndex = static_cast<int>(renderMode_);
	if (InspectorUI::Combo("Render Mode", &renderModeIndex, renderModeItems, 2)) {
		renderMode_ = static_cast<RenderMode>(renderModeIndex);
	}

	// 使わない項目は出さない(UnityのCanvasインスペクタと同じ挙動)。
	if (renderMode_ == RenderMode::WorldSpace) {
		InspectorUI::DragFloat2("Canvas Size (W,H)", &worldCanvasSize_.x, 1.0f, 1.0f, 8192.0f);
		InspectorUI::TextDisabled("配置はGameObjectのTransform。world単位への変換はscaleで行う(例: 0.01)。");
	} else {
		InspectorUI::DragFloat("Reference Width", &referenceResolution_.x, 1.0f, 1.0f, 8192.0f);
		InspectorUI::DragFloat("Reference Height", &referenceResolution_.y, 1.0f, 1.0f, 8192.0f);
		InspectorUI::Checkbox("Scale With Screen Size", &scaleWithScreenSize_);
		InspectorUI::DragFloat("Match (W<->H)", &matchWidthHeight_, 0.01f, 0.0f, 1.0f);
	}

	InspectorUI::DragInt("Sort Order", &sortOrder_, 1.0f, -100, 100);
	InspectorUI::ItemTooltip("描画順(大きいほど手前)。\n"
	                         "パッド操作のフォーカスは「ボタンを持つ最前面のCanvas」だけが受け取るので、\n"
	                         "ポーズ/死亡メニューはHUDより大きい値にすること。");

	// --- ゲームパッド/キーボードのフォーカス操作 ---
	{
		void* dropped = nullptr;
		bool cleared = false;
		const std::string name = GameObjectDisplayName(firstSelected_.value);
		if (InspectorUI::ObjectField("First Selected", name.c_str(), &dropped, &cleared)) {
			if (cleared) {
				firstSelected_.Clear();
			} else if (dropped) {
				firstSelected_.Assign(static_cast<GameObject*>(dropped));
			}
		}
		InspectorUI::ItemTooltip("このCanvasがフォーカスを得たとき最初に選ぶボタン。\n未設定なら描画順で最初のボタンが選ばれる。");
	}
	DrawUnityActionInspector("On Cancel ()", "onCancel", onCancel_);
#endif // USE_IMGUI
}

void CanvasComponent::WriteJson(nlohmann::json& json) const {
	json["renderMode"] = static_cast<int>(renderMode_);
	json["worldCanvasSize"] = {worldCanvasSize_.x, worldCanvasSize_.y};
	json["referenceResolution"] = {referenceResolution_.x, referenceResolution_.y};
	json["matchWidthHeight"] = matchWidthHeight_;
	json["scaleWithScreenSize"] = scaleWithScreenSize_;
	json["sortOrder"] = sortOrder_;
	json["firstSelected"] = firstSelected_.targetInstanceId;
	WriteUnityActionJson(json, "onCancel", onCancel_);
}

void CanvasComponent::ReadJson(const nlohmann::json& json) {
	// キーが無い旧データはScreen Space - Overlay(従来の挙動)のまま。
	if (json.contains("renderMode") && json.at("renderMode").is_number_integer()) {
		const int value = json.at("renderMode").get<int>();
		if (value == static_cast<int>(RenderMode::WorldSpace)) {
			renderMode_ = RenderMode::WorldSpace;
		} else {
			renderMode_ = RenderMode::ScreenSpaceOverlay;
		}
	}
	worldCanvasSize_ = ReadVector2(json, "worldCanvasSize", worldCanvasSize_);
	referenceResolution_ = ReadVector2(json, "referenceResolution", referenceResolution_);
	matchWidthHeight_ = ReadFloat(json, "matchWidthHeight", matchWidthHeight_);
	if (json.contains("scaleWithScreenSize") && json.at("scaleWithScreenSize").is_boolean()) {
		scaleWithScreenSize_ = json.at("scaleWithScreenSize").get<bool>();
	}
	if (json.contains("sortOrder") && json.at("sortOrder").is_number_integer()) {
		sortOrder_ = json.at("sortOrder").get<int>();
	}
	firstSelected_.targetInstanceId = ReadString(json, "firstSelected", "");
	ReadUnityActionJson(json, "onCancel", onCancel_);
}

void CanvasComponent::ResolveReferences(IObjectResolver& resolver) {
	firstSelected_.Resolve(resolver);
	onCancel_.Resolve(resolver);
}

} // namespace KujataEngine
