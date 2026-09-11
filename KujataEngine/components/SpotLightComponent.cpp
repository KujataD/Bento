#include "SpotLightComponent.h"

#include "../math/MathUtil.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"

#include <algorithm>
#include <cmath>

namespace KujataEngine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

Vector4 ReadVector4(const nlohmann::json& json, const char* key, const Vector4& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 4) {
		return defaultValue;
	}
	if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number()) {
		return defaultValue;
	}
	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

} // namespace

SpotLightComponent::SpotLightComponent() {
	data_.intensity = 1.0f;
	data_.distance = 10.0f;
	data_.decay = 1.0f;
	data_.direction = {0.0f, 0.0f, 1.0f};
	ApplyAnglesToData();
}

Vector3 SpotLightComponent::GetWorldDirection() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return {0.0f, 0.0f, 1.0f};
	}

	// 行ベクトル規約なので、ワールド行列の3行目がローカル+Z軸(前方)になる。
	const Matrix4x4& world = owner->GetTransform().matWorld_;
	Vector3 forward = {world.m[2][0], world.m[2][1], world.m[2][2]};
	if (Length(forward) <= 0.0f) {
		return {0.0f, 0.0f, 1.0f};
	}
	return Normalize(forward);
}

void SpotLightComponent::ApplyAnglesToData() {
	// Unityと同じく全開き角で持つので、cosineにするときは半分にする。
	// innerがouterを超えると減衰が反転するためクランプしておく。
	spotAngleDegrees_ = std::clamp(spotAngleDegrees_, 1.0f, 179.0f);
	innerSpotAngleDegrees_ = std::clamp(innerSpotAngleDegrees_, 0.0f, spotAngleDegrees_);

	const float toRadianHalf = (kPi / 180.0f) * 0.5f;
	data_.cosAngle = std::cos(spotAngleDegrees_ * toRadianHalf);
	data_.cosFalloffStart = std::cos(innerSpotAngleDegrees_ * toRadianHalf);
}

void SpotLightComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::ColorEdit4("Color", &data_.color.x);
	InspectorUI::DragFloat("Intensity", &data_.intensity, 0.01f, 0.0f, 100.0f);
	InspectorUI::DragFloat("Range", &data_.distance, 0.05f, 0.0f, 1000.0f);
	InspectorUI::DragFloat("Spot Angle", &spotAngleDegrees_, 0.5f, 1.0f, 179.0f);
	InspectorUI::DragFloat("Inner Spot Angle", &innerSpotAngleDegrees_, 0.5f, 0.0f, 179.0f);
	InspectorUI::DragFloat("Decay", &data_.decay, 0.01f, 0.0f, 100.0f);
	InspectorUI::TextDisabled("Position / Direction are read from Transform (+Z).");

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
	if (data_.distance < 0.0f) {
		data_.distance = 0.0f;
	}
	if (data_.decay < 0.0f) {
		data_.decay = 0.0f;
	}
	ApplyAnglesToData();
#endif // USE_IMGUI
}

void SpotLightComponent::Apply() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	owner->UpdateWorldTransformSelfAndAncestors();
	data_.position = owner->GetTransform().GetWorldPosition();
	data_.direction = GetWorldDirection();
	ApplyAnglesToData();

	// 毎フレームResetされた配列へ積む(PointLightと同じ流儀)。上限はkMaxSpotLight。
	SpotLight::GetInstance()->AddLight(data_);
}

void SpotLightComponent::WriteJson(nlohmann::json& json) const {
	json["color"] = {data_.color.x, data_.color.y, data_.color.z, data_.color.w};
	json["intensity"] = data_.intensity;
	json["range"] = data_.distance;
	json["spotAngle"] = spotAngleDegrees_;
	json["innerSpotAngle"] = innerSpotAngleDegrees_;
	json["decay"] = data_.decay;
}

void SpotLightComponent::ReadJson(const nlohmann::json& json) {
	data_.color = ReadVector4(json, "color", data_.color);
	data_.intensity = ReadFloat(json, "intensity", data_.intensity);
	data_.distance = ReadFloat(json, "range", data_.distance);
	spotAngleDegrees_ = ReadFloat(json, "spotAngle", spotAngleDegrees_);
	innerSpotAngleDegrees_ = ReadFloat(json, "innerSpotAngle", innerSpotAngleDegrees_);
	data_.decay = ReadFloat(json, "decay", data_.decay);

	if (data_.intensity < 0.0f) {
		data_.intensity = 0.0f;
	}
	if (data_.distance < 0.0f) {
		data_.distance = 0.0f;
	}
	if (data_.decay < 0.0f) {
		data_.decay = 0.0f;
	}
	ApplyAnglesToData();
}

} // namespace KujataEngine
