#include "TransformComponent.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"

namespace KujataEngine {

namespace {

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

} // namespace

WorldTransform& TransformComponent::GetTransform() {
	GameObject* owner = GetOwner();
	if (owner) {
		return owner->GetTransform();
	}
	return fallbackTransform_;
}

const WorldTransform& TransformComponent::GetTransform() const {
	const GameObject* owner = GetOwner();
	if (owner) {
		return owner->GetTransform();
	}
	return fallbackTransform_;
}

void TransformComponent::Initialize() {
	if (initialized_) {
		return;
	}

	initialized_ = true;
}

void TransformComponent::DrawInspector() {
#ifdef USE_IMGUI
	WorldTransform& transform = GetTransform();
	bool translationChanged = InspectorUI::DragFloat3("Translation", &transform.translation_.x, 0.01f);
	InspectorUI::AnimationFieldHook("translation", &transform.translation_.x, 3, translationChanged);
	bool rotationChanged = InspectorUI::DragFloat3("Rotation", &transform.rotation_.x, 0.01f);
	InspectorUI::AnimationFieldHook("rotation", &transform.rotation_.x, 3, rotationChanged);
	bool scaleChanged = InspectorUI::DragFloat3("Scale", &transform.scale_.x, 0.01f);
	InspectorUI::AnimationFieldHook("scale", &transform.scale_.x, 3, scaleChanged);

	if (transform.scale_.x < 0.001f) {
		transform.scale_.x = 0.001f;
	}
	if (transform.scale_.y < 0.001f) {
		transform.scale_.y = 0.001f;
	}
	if (transform.scale_.z < 0.001f) {
		transform.scale_.z = 0.001f;
	}
#endif
}

void TransformComponent::CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) {
	// JSONキー(translation/rotation/scale)と揃えたチャンネル名にする。
	WorldTransform& transform = GetTransform();
	channels.push_back({"translation.x", &transform.translation_.x});
	channels.push_back({"translation.y", &transform.translation_.y});
	channels.push_back({"translation.z", &transform.translation_.z});
	channels.push_back({"rotation.x", &transform.rotation_.x});
	channels.push_back({"rotation.y", &transform.rotation_.y});
	channels.push_back({"rotation.z", &transform.rotation_.z});
	channels.push_back({"scale.x", &transform.scale_.x});
	channels.push_back({"scale.y", &transform.scale_.y});
	channels.push_back({"scale.z", &transform.scale_.z});

	// 向き基準の移動チャンネル(moveForward/moveRight)はここには置かない。
	// アニメーション専用の概念のため、AnimatorComponentが仮想チャンネルとして直接解決・適用する。
}

void TransformComponent::WriteJson(nlohmann::json& json) const {
	const WorldTransform& transform = GetTransform();
	json["translation"] = {transform.translation_.x, transform.translation_.y, transform.translation_.z};
	json["rotation"] = {transform.rotation_.x, transform.rotation_.y, transform.rotation_.z};
	json["scale"] = {transform.scale_.x, transform.scale_.y, transform.scale_.z};
}

void TransformComponent::ReadJson(const nlohmann::json& json) {
	WorldTransform& transform = GetTransform();
	transform.translation_ = ReadVector3(json, "translation", transform.translation_);
	transform.rotation_ = ReadVector3(json, "rotation", transform.rotation_);
	transform.scale_ = ReadVector3(json, "scale", transform.scale_);

	if (transform.scale_.x < 0.001f) {
		transform.scale_.x = 0.001f;
	}
	if (transform.scale_.y < 0.001f) {
		transform.scale_.y = 0.001f;
	}
	if (transform.scale_.z < 0.001f) {
		transform.scale_.z = 0.001f;
	}
}

} // namespace KujataEngine
