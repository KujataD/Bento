#include "CameraComponent.h"
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

} // namespace

void CameraComponent::Initialize() {
	if (initialized_) {
		return;
	}

	camera_.Initialize();
	initialized_ = true;
	SyncFromOwnerTransform();
}

void CameraComponent::Update() {
	SyncFromOwnerTransform();
}

void CameraComponent::Draw() {
	SyncFromOwnerTransform();
}

void CameraComponent::DrawInspector() {
#ifdef USE_IMGUI
	InspectorUI::DragFloat("Fov Y", &camera_.fovAngleY, 0.001f, 0.01f, 3.13f);
	InspectorUI::DragFloat("Aspect", &camera_.aspectRatio, 0.001f, 0.01f, 10.0f);
	InspectorUI::DragFloat("Near Z", &camera_.nearZ, 0.001f, 0.001f, 1000.0f);
	InspectorUI::DragFloat("Far Z", &camera_.farZ, 1.0f, 0.01f, 100000.0f);

	if (camera_.fovAngleY < 0.01f) {
		camera_.fovAngleY = 0.01f;
	}
	if (camera_.aspectRatio < 0.01f) {
		camera_.aspectRatio = 0.01f;
	}
	if (camera_.nearZ < 0.001f) {
		camera_.nearZ = 0.001f;
	}
	if (camera_.farZ <= camera_.nearZ) {
		camera_.farZ = camera_.nearZ + 0.001f;
	}
#endif // USE_IMGUI
}

void CameraComponent::WriteJson(nlohmann::json& json) const {
	json["fovAngleY"] = camera_.fovAngleY;
	json["aspectRatio"] = camera_.aspectRatio;
	json["nearZ"] = camera_.nearZ;
	json["farZ"] = camera_.farZ;
}

void CameraComponent::ReadJson(const nlohmann::json& json) {
	camera_.fovAngleY = ReadFloat(json, "fovAngleY", camera_.fovAngleY);
	camera_.aspectRatio = ReadFloat(json, "aspectRatio", camera_.aspectRatio);
	camera_.nearZ = ReadFloat(json, "nearZ", camera_.nearZ);
	camera_.farZ = ReadFloat(json, "farZ", camera_.farZ);

	if (camera_.fovAngleY < 0.01f) {
		camera_.fovAngleY = 0.01f;
	}
	if (camera_.aspectRatio < 0.01f) {
		camera_.aspectRatio = 0.01f;
	}
	if (camera_.nearZ < 0.001f) {
		camera_.nearZ = 0.001f;
	}
	if (camera_.farZ <= camera_.nearZ) {
		camera_.farZ = camera_.nearZ + 0.001f;
	}
}

void CameraComponent::OnAfterReadJson() {
	SyncFromOwnerTransform();
}

void CameraComponent::SyncFromOwnerTransform() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	if (!initialized_) {
		return;
	}

	const WorldTransform& transform = owner->GetTransform();
	owner->UpdateWorldTransformSelfAndAncestors();
	camera_.translation_ = transform.GetWorldPosition();
	camera_.rotation_ = transform.rotation_;
	camera_.UpdateMatrix();
}

} // namespace KujataEngine
