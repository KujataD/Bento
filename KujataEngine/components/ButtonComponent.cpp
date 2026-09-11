#include "ButtonComponent.h"

#include "../runtime/InspectorUI.h"
#include "../runtime/UIEventBus.h"
#include "../scene/GameObject.h"
#include "../scene/InvokableMethod.h"
#include "../scene/UnityActionUI.h"
#include "ImageComponent.h"
#include <cstring>
#include <string>

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

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

/// <summary>ObjectRefのInspector欄(D&Dで代入 / Clearで解除)。</summary>
void DrawObjectRefField([[maybe_unused]] const char* label, [[maybe_unused]] ObjectRef& ref) {
#ifdef USE_IMGUI
	void* dropped = nullptr;
	bool cleared = false;
	const std::string name = GameObjectDisplayName(ref.value);
	if (InspectorUI::ObjectField(label, name.c_str(), &dropped, &cleared)) {
		if (cleared) {
			ref.Clear();
		} else if (dropped) {
			ref.Assign(static_cast<GameObject*>(dropped));
		}
	}
#endif // USE_IMGUI
}

} // namespace

void ButtonComponent::SyncEventBuffer() {
	std::memset(eventBuffer_.data(), 0, eventBuffer_.size());
	strncpy_s(eventBuffer_.data(), eventBuffer_.size(), onClickEvent_.c_str(), _TRUNCATE);
}

GameObject* ButtonComponent::GetExplicitNeighbor(UINavDirection direction) const {
	switch (direction) {
	case UINavDirection::Up:
		return selectOnUp_.value;
	case UINavDirection::Down:
		return selectOnDown_.value;
	case UINavDirection::Left:
		return selectOnLeft_.value;
	case UINavDirection::Right:
		return selectOnRight_.value;
	}
	return nullptr;
}

void ButtonComponent::ApplyVisualState(VisualState state) {
	if (!owner_) {
		return;
	}
	ImageComponent* image = owner_->GetComponent<ImageComponent>();
	if (!image) {
		return;
	}
	if (!interactable_) {
		state = VisualState::Disabled;
	}
	switch (state) {
	case VisualState::Highlighted:
		image->SetColor(highlightedColor_);
		break;
	case VisualState::Pressed:
		image->SetColor(pressedColor_);
		break;
	case VisualState::Disabled:
		image->SetColor(disabledColor_);
		break;
	case VisualState::Normal:
	default:
		image->SetColor(normalColor_);
		break;
	}
}

void ButtonComponent::FireOnClick() {
	if (!interactable_) {
		return;
	}
	// 旧来の名前付きイベント(後方互換)。
	UIEventBus::Publish(onClickEvent_);
	// UnityのButton.onClick相当。対象Componentのメソッドを直接呼ぶ。
	onClick_.Invoke();
}

void ButtonComponent::DrawInspector() {
#ifdef USE_IMGUI
	if (!eventBufferSynced_) {
		SyncEventBuffer();
		eventBufferSynced_ = true;
	}
	InspectorUI::Checkbox("Interactable", &interactable_);
	InspectorUI::ColorEdit4("Normal", &normalColor_.x);
	InspectorUI::ColorEdit4("Highlighted", &highlightedColor_.x);
	InspectorUI::ColorEdit4("Pressed", &pressedColor_.x);
	InspectorUI::ColorEdit4("Disabled", &disabledColor_.x);

	const char* navigationItems[] = {"None", "Automatic", "Explicit"};
	int navigationIndex = static_cast<int>(navigationMode_);
	if (InspectorUI::Combo("Navigation", &navigationIndex, navigationItems, 3)) {
		navigationMode_ = static_cast<NavigationMode>(navigationIndex);
	}
	InspectorUI::ItemTooltip("ゲームパッド/キーボードでのフォーカス移動の解決方法。\n"
	                         "None=対象外(マウス専用) / Automatic=矩形の位置から自動 / Explicit=移動先を明示指定。");
	if (navigationMode_ == NavigationMode::Explicit) {
		DrawObjectRefField("Select On Up", selectOnUp_);
		DrawObjectRefField("Select On Down", selectOnDown_);
		DrawObjectRefField("Select On Left", selectOnLeft_);
		DrawObjectRefField("Select On Right", selectOnRight_);
	}

	if (InspectorUI::InputText("On Click Event", eventBuffer_.data(), eventBuffer_.size())) {
		onClickEvent_ = eventBuffer_.data();
	}
	DrawUnityActionInspector("On Click ()", "onClick", onClick_);
#endif // USE_IMGUI
}

void ButtonComponent::WriteJson(nlohmann::json& json) const {
	json["interactable"] = interactable_;
	json["normalColor"] = {normalColor_.x, normalColor_.y, normalColor_.z, normalColor_.w};
	json["highlightedColor"] = {highlightedColor_.x, highlightedColor_.y, highlightedColor_.z, highlightedColor_.w};
	json["pressedColor"] = {pressedColor_.x, pressedColor_.y, pressedColor_.z, pressedColor_.w};
	json["disabledColor"] = {disabledColor_.x, disabledColor_.y, disabledColor_.z, disabledColor_.w};
	json["onClickEvent"] = onClickEvent_;
	json["navigationMode"] = static_cast<int>(navigationMode_);
	json["selectOnUp"] = selectOnUp_.targetInstanceId;
	json["selectOnDown"] = selectOnDown_.targetInstanceId;
	json["selectOnLeft"] = selectOnLeft_.targetInstanceId;
	json["selectOnRight"] = selectOnRight_.targetInstanceId;

	WriteUnityActionJson(json, "onClick", onClick_);
}

void ButtonComponent::ReadJson(const nlohmann::json& json) {
	if (json.contains("interactable") && json.at("interactable").is_boolean()) {
		interactable_ = json.at("interactable").get<bool>();
	}
	normalColor_ = ReadVector4(json, "normalColor", normalColor_);
	highlightedColor_ = ReadVector4(json, "highlightedColor", highlightedColor_);
	pressedColor_ = ReadVector4(json, "pressedColor", pressedColor_);
	disabledColor_ = ReadVector4(json, "disabledColor", disabledColor_);
	onClickEvent_ = ReadString(json, "onClickEvent", onClickEvent_);

	// キーが無い旧データはAutomatic(既定値)のまま = パッドで操作できる。
	if (json.contains("navigationMode") && json.at("navigationMode").is_number_integer()) {
		const int value = json.at("navigationMode").get<int>();
		if (value >= static_cast<int>(NavigationMode::None) && value <= static_cast<int>(NavigationMode::Explicit)) {
			navigationMode_ = static_cast<NavigationMode>(value);
		}
	}
	selectOnUp_.targetInstanceId = ReadString(json, "selectOnUp", "");
	selectOnDown_.targetInstanceId = ReadString(json, "selectOnDown", "");
	selectOnLeft_.targetInstanceId = ReadString(json, "selectOnLeft", "");
	selectOnRight_.targetInstanceId = ReadString(json, "selectOnRight", "");

	ReadUnityActionJson(json, "onClick", onClick_);
}

void ButtonComponent::OnAfterReadJson() {
	SyncEventBuffer();
	eventBufferSynced_ = true;
}

void ButtonComponent::ResolveReferences(IObjectResolver& resolver) {
	// onClickの各target(instanceId)を実GameObject*へ解決する。
	onClick_.Resolve(resolver);
	// Explicitナビゲーションの移動先も同様に解決する。
	selectOnUp_.Resolve(resolver);
	selectOnDown_.Resolve(resolver);
	selectOnLeft_.Resolve(resolver);
	selectOnRight_.Resolve(resolver);
}

} // namespace KujataEngine
