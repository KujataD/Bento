#include "VolumeComponent.h"

#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "ColliderComponent.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif // USE_IMGUI

namespace KujataEngine {

namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

bool ReadBool(const nlohmann::json& json, const char* key, bool defaultValue) {
	if (!json.contains(key) || !json.at(key).is_boolean()) {
		return defaultValue;
	}
	return json.at(key).get<bool>();
}

} // namespace

void VolumeComponent::DrawInspector() {
#ifdef USE_IMGUI
	// --- Volume自体の効き方 ---
	const char* modeItems[] = {"Global", "Local"};
	int modeIndex = isGlobal_ ? 0 : 1;
	if (ImGui::Combo("Mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems))) {
		isGlobal_ = (modeIndex == 0);
	}

	ImGui::DragFloat("Priority", &priority_, 0.1f);
	ImGui::SliderFloat("Weight", &weight_, 0.0f, 1.0f);

	if (!isGlobal_) {
		ImGui::DragFloat("Blend Distance", &blendDistance_, 0.05f, 0.0f, 100.0f);
		// Localは同じGameObjectのColliderを範囲に使うため、無いと一切効かない。
		ColliderComponent* collider = owner_ ? owner_->GetComponent<ColliderComponent>() : nullptr;
		if (!collider) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Local には Collider が必要です");
		}
	}

	ImGui::Separator();

	// --- エフェクト別の設定 ---
	// 表示・overrideの扱いはディスクリプタ表を回すだけなので、
	// エフェクトが増えてもこのループを触る必要はない。
	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		ImGui::PushID(descriptor.name);

		bool overrideState = profile_.IsOverriding(descriptor.id);
		if (ImGui::Checkbox("##override", &overrideState)) {
			profile_.SetOverriding(descriptor.id, overrideState);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("このVolumeで %s を上書きする", descriptor.name);
		}
		ImGui::SameLine();

		if (ImGui::CollapsingHeader(descriptor.name)) {
			ImGui::Indent();
			// override無効の項目は編集させない(見えてはいるが効かないことを示す)。
			ImGui::BeginDisabled(!overrideState);
			descriptor.drawInspector(profile_);
			ImGui::EndDisabled();
			ImGui::Unindent();
		}

		ImGui::PopID();
	}
#endif // USE_IMGUI
}

void VolumeComponent::WriteJson(nlohmann::json& json) const {
	json["isGlobal"] = isGlobal_;
	json["priority"] = priority_;
	json["weight"] = weight_;
	json["blendDistance"] = blendDistance_;

	nlohmann::json profileJson = nlohmann::json::object();
	WriteVolumeProfileJson(profile_, profileJson);
	json["profile"] = profileJson;
}

void VolumeComponent::ReadJson(const nlohmann::json& json) {
	isGlobal_ = ReadBool(json, "isGlobal", isGlobal_);
	priority_ = ReadFloat(json, "priority", priority_);
	weight_ = ReadFloat(json, "weight", weight_);
	blendDistance_ = ReadFloat(json, "blendDistance", blendDistance_);

	if (json.contains("profile") && json.at("profile").is_object()) {
		ReadVolumeProfileJson(json.at("profile"), profile_);
	}
}

GameObject* CreateGlobalVolumeObject(Scene& scene) {
	GameObject* volumeObject = scene.CreateGameObject("Global Volume");
	if (!volumeObject) {
		return nullptr;
	}

	VolumeComponent* volume = volumeObject->AddComponent<VolumeComponent>();
	if (volume) {
		// 全エフェクトがoverride無しだと置いても何も起きないため、Tonemapだけ立てておく。
		// 残りはInspectorのチェックボックスで必要なものをONにしてもらう。
		volume->GetProfile().SetOverriding(VolumeEffectId::kTonemap, true);
		scene.OnEditorComponentAdded(volumeObject, volume);
	}
	return volumeObject;
}

} // namespace KujataEngine
