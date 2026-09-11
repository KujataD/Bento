#include "DirectionalLightComponent.h"
#include "../runtime/InspectorUI.h"
#include "../math/MathUtil.h"

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

Vector3 ReadVector3(const nlohmann::json& json, const char* key, const Vector3& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}

	const nlohmann::json& value = json.at(key);
	if (!value.is_array()) {
		return defaultValue;
	}
	if (value.size() < 3) {
		return defaultValue;
	}
	if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
		return defaultValue;
	}

	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
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

void DirectionalLightComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::ColorEdit4("Color", &data_.color.x);
	InspectorUI::DragFloat3("Direction", &data_.direction.x, 0.01f);
	InspectorUI::DragFloat("Intensity", &data_.intensity, 0.01f, 0.0f, 100.0f);

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
#endif // USE_IMGUI
}

void DirectionalLightComponent::Apply() {
	if (Length(data_.direction) == 0.0f) {
		data_.direction = {0.0f, -1.0f, 0.0f};
	} else {
		data_.direction = Normalize(data_.direction);
	}

	DirectionalLight::GetInstance()->GetData() = data_;
	DirectionalLight::GetInstance()->Update();
}

void DirectionalLightComponent::WriteJson(nlohmann::json& json) const {
	json["color"] = {data_.color.x, data_.color.y, data_.color.z, data_.color.w};
	json["direction"] = {data_.direction.x, data_.direction.y, data_.direction.z};
	json["intensity"] = data_.intensity;
}

void DirectionalLightComponent::ReadJson(const nlohmann::json& json) {
	data_.color = ReadVector4(json, "color", data_.color);
	data_.direction = ReadVector3(json, "direction", data_.direction);
	data_.intensity = ReadFloat(json, "intensity", data_.intensity);

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
}

void DirectionalLightComponent::OnAfterReadJson() {
	Apply();
}

} // namespace KujataEngine
