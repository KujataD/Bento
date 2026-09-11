#include "ImageComponent.h"

#include "../base/TextureManager.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"
#include "RectTransformComponent.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace KujataEngine {
namespace {

Vector4 ReadVector4(const nlohmann::json& json, const char* key, const Vector4& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 4) {
		return defaultValue;
	}
	for (int i = 0; i < 4; ++i) {
		if (!value[i].is_number()) {
			return defaultValue;
		}
	}
	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

Vector2 ReadVector2(const nlohmann::json& json, const char* key, const Vector2& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 2) {
		return defaultValue;
	}
	for (int i = 0; i < 2; ++i) {
		if (!value[i].is_number()) {
			return defaultValue;
		}
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

void ImageComponent::SyncPathBuffer() {
	std::memset(pathBuffer_.data(), 0, pathBuffer_.size());
	strncpy_s(pathBuffer_.data(), pathBuffer_.size(), texturePath_.c_str(), _TRUNCATE);
}

void ImageComponent::SetTexture(const std::string& assetId, const std::string& path) {
	textureAssetId_ = assetId;
	texturePath_ = path;

	// パスだけの参照はassetIdを補完し、リネーム/移動に追従できる参照へ揃える。
	// Editor外(FallbackAssetResolver)ではIDが取れないため、その場合はパスをそのまま保持する。
	if (textureAssetId_.empty() && !texturePath_.empty()) {
		IAssetResolver& assetDatabase = GetAssetResolver();
		std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", texturePath_);
		textureAssetId_ = assetDatabase.GetOrCreateAssetId(resolvedPath);
		if (!textureAssetId_.empty()) {
			texturePath_ = assetDatabase.MakeProjectRelativePath(resolvedPath);
		}
	}

	textureResolved_ = false;
	SyncPathBuffer();
}

void ImageComponent::SetTextureIndexDirect(uint32_t textureIndex) {
	textureIndex_ = textureIndex;
	textureIndexOverridden_ = true;
	// DrawUIの「未Prepareなら白へフォールバック」に巻き戻されないよう解決済みにする。
	textureResolved_ = true;
}

void ImageComponent::EnsureTextureLoaded() {
	if (textureIndexOverridden_) {
		return;
	}
	// assetId優先で現在のパスへ解決する(移動済みアセットは.meta経由で新パスが返る)。
	std::string resolvedPath;
	if (!textureAssetId_.empty() || !texturePath_.empty()) {
		resolvedPath = GetAssetResolver().ResolveAssetPath(textureAssetId_, texturePath_).string();
	}
	if (textureResolved_ && loadedPath_ == resolvedPath) {
		return;
	}
	TextureManager* textureManager = TextureManager::GetInstance();
	uint32_t index = textureManager->GetDefaultWhiteTexture();
	if (!resolvedPath.empty()) {
		uint32_t loaded = 0;
		if (textureManager->TryLoadTexture(resolvedPath, loaded)) {
			index = loaded;
		}
	}
	textureIndex_ = index;
	loadedPath_ = resolvedPath;
	textureResolved_ = true;
}

void ImageComponent::Prepare() {
	// テクスチャ読み込み(コマンドリスト実行を伴う)は描画パス外のここで行う。
	EnsureTextureLoaded();
}

void ImageComponent::SetFillAmount(float fillAmount) {
	fillAmount_ = std::clamp(fillAmount, 0.0f, 1.0f);
}

void ImageComponent::DrawUI(const UIRect& canvasRect, float scaleFactor) {
	if (!quadInitialized_) {
		quad_.Initialize();
		quadInitialized_ = true;
		SyncPathBuffer();
	}
	// 未Prepareでも安全なようにキャッシュ済み白テクスチャへフォールバック(新規読み込みはしない)。
	if (!textureResolved_) {
		textureIndex_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
	}

	// Fill: 左端固定で横幅とUVをfill率だけ縮める(UnityのImage.fillAmount水平相当)。
	float fill = std::clamp(fillAmount_, 0.0f, 1.0f);
	if (fill <= 0.0f) {
		return;
	}

	// RectTransformのRotation Zは自分の矩形の中心まわりに掛ける(子へは伝播しない)。
	float rotation = 0.0f;
	if (owner_) {
		if (RectTransformComponent* rectTransform = owner_->GetComponent<RectTransformComponent>()) {
			rotation = rectTransform->GetRotationZ();
		}
	}
	quad_.SetRect(canvasRect.x * scaleFactor, canvasRect.y * scaleFactor, canvasRect.width * scaleFactor * fill, canvasRect.height * scaleFactor, rotation);
	quad_.SetUV({0.0f, 0.0f}, {fill, 1.0f});
	// UVトランスフォームは頂点UV(Fill適用後)にシェーダー側で掛かる。スクロール演出やアトラス部分表示用。
	quad_.SetUVTransform(uvOffset_, uvScale_, uvRotation_);
	quad_.SetColor(color_);
	quad_.SetTexture(textureIndex_);
	quad_.Draw();
}

void ImageComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::ColorEdit4("Color", &color_.x);
	if (InspectorUI::InputText("Texture", pathBuffer_.data(), pathBuffer_.size())) {
		// SetTextureが存在するパスならassetIdを補完する(入力途中の不完全なパスはID無しのまま)。
		SetTexture("", pathBuffer_.data());
	}
	InspectorUI::Checkbox("Raycast Target", &raycastTarget_);
	if (InspectorUI::DragFloat("Fill Amount", &fillAmount_, 0.01f, 0.0f, 1.0f)) {
		fillAmount_ = std::clamp(fillAmount_, 0.0f, 1.0f);
	}

	InspectorUI::TextUnformatted("--- UV Transform ---");
	InspectorUI::DragFloat2("UV Offset", &uvOffset_.x, 0.01f);
	InspectorUI::DragFloat2("UV Scale", &uvScale_.x, 0.01f);
	// ラジアン指定(Transformのrotation_と同じ単位に揃える)。
	InspectorUI::DragFloat("UV Rotation", &uvRotation_, 0.01f);
#endif // USE_IMGUI
}

void ImageComponent::WriteJson(nlohmann::json& json) const {
	json["textureAssetId"] = textureAssetId_;
	json["texturePath"] = texturePath_;
	json["color"] = {color_.x, color_.y, color_.z, color_.w};
	json["raycastTarget"] = raycastTarget_;
	json["fillAmount"] = fillAmount_;
	json["uvOffset"] = {uvOffset_.x, uvOffset_.y};
	json["uvScale"] = {uvScale_.x, uvScale_.y};
	json["uvRotation"] = uvRotation_;
}

void ImageComponent::ReadJson(const nlohmann::json& json) {
	// SetTexture経由にすることで、ID未付与の旧データはここで補完される(次回保存時に永続化)。
	SetTexture(ReadString(json, "textureAssetId", textureAssetId_), ReadString(json, "texturePath", texturePath_));
	color_ = ReadVector4(json, "color", color_);
	if (json.contains("raycastTarget") && json.at("raycastTarget").is_boolean()) {
		raycastTarget_ = json.at("raycastTarget").get<bool>();
	}
	if (json.contains("fillAmount") && json.at("fillAmount").is_number()) {
		fillAmount_ = std::clamp(json.at("fillAmount").get<float>(), 0.0f, 1.0f);
	}
	uvOffset_ = ReadVector2(json, "uvOffset", uvOffset_);
	uvScale_ = ReadVector2(json, "uvScale", uvScale_);
	if (json.contains("uvRotation") && json.at("uvRotation").is_number()) {
		uvRotation_ = json.at("uvRotation").get<float>();
	}
	textureResolved_ = false;
}

void ImageComponent::OnAfterReadJson() {
	SyncPathBuffer();
	textureResolved_ = false;
}

} // namespace KujataEngine
