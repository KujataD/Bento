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
	// ドット絵化(メインカメラのときだけ効く)。1で通常の解像度。
	int pixelSize = camera_.pixelSize;
	if (InspectorUI::DragInt("Pixel Size", &pixelSize, 0.05f, 1, 32)) {
		camera_.pixelSize = pixelSize;
	}

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
	json["pixelSize"] = camera_.pixelSize;
}

void CameraComponent::ReadJson(const nlohmann::json& json) {
	camera_.fovAngleY = ReadFloat(json, "fovAngleY", camera_.fovAngleY);
	camera_.aspectRatio = ReadFloat(json, "aspectRatio", camera_.aspectRatio);
	camera_.nearZ = ReadFloat(json, "nearZ", camera_.nearZ);
	camera_.farZ = ReadFloat(json, "farZ", camera_.farZ);
	// ドット絵化の大きさ(キーが無い旧シーンは 1 = 通常の解像度)。
	if (json.contains("pixelSize") && json.at("pixelSize").is_number()) {
		camera_.pixelSize = static_cast<int32_t>(json.at("pixelSize").get<double>());
	}
	if (camera_.pixelSize < 1) {
		camera_.pixelSize = 1;
	}

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
