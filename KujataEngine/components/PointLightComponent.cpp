#include "PointLightComponent.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"

namespace KujataEngine {

namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_number()) {
		return defaultValue;
	}

	return json.at(key).get<float>();
}

Vector4 ReadVector4(const nlohmann::json& json, const char* key, const Vector4& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}

	const nlohmann::json& value = json.at(key);
	if (!value.is_array()) {
		return defaultValue;
	}
	if (value.size() < 4) {
		return defaultValue;
	}
	if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number()) {
		return defaultValue;
	}

	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

} // namespace

void PointLightComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::ColorEdit4("Color", &data_.color.x);
	InspectorUI::DragFloat("Intensity", &data_.intensity, 0.01f, 0.0f, 100.0f);
	InspectorUI::DragFloat("Radius", &data_.radius, 0.01f, 0.0f, 1000.0f);
	InspectorUI::DragFloat("Decay", &data_.decay, 0.01f, 0.0f, 100.0f);
	InspectorUI::TextDisabled("Position is read from Transform.");

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
	if (data_.radius < 0.0f) {
		data_.radius = 0.0f;
	}
	if (data_.decay < 0.0f) {
		data_.decay = 0.0f;
	}
#endif // USE_IMGUI
}

void PointLightComponent::Apply() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	owner->UpdateWorldTransformSelfAndAncestors();
	data_.position = owner->GetTransform().GetWorldPosition();
	PointLight::GetInstance()->AddLight(data_);
}

void PointLightComponent::WriteJson(nlohmann::json& json) const {
	json["color"] = {data_.color.x, data_.color.y, data_.color.z, data_.color.w};
	json["intensity"] = data_.intensity;
	json["radius"] = data_.radius;
	json["decay"] = data_.decay;
}

void PointLightComponent::ReadJson(const nlohmann::json& json) {
	data_.color = ReadVector4(json, "color", data_.color);
	data_.intensity = ReadFloat(json, "intensity", data_.intensity);
	data_.radius = ReadFloat(json, "radius", data_.radius);
	data_.decay = ReadFloat(json, "decay", data_.decay);

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
	if (data_.radius < 0.0f) {
		data_.radius = 0.0f;
	}
	if (data_.decay < 0.0f) {
		data_.decay = 0.0f;
	}
}

} // namespace KujataEngine
