#include "DebugCameraComponent.h"
#include "CameraComponent.h"
#include "../runtime/PlayState.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"
#include <cmath>

namespace KujataEngine {

namespace {

bool NearlyEqual(float a, float b) {
	return std::fabs(a - b) < 0.0001f;
}

} // namespace

void DebugCameraComponent::Initialize() {
	InitializeFromOwnerTransform();
}

void DebugCameraComponent::DrawInspector() {
#ifdef USE_IMGUI
	GameObject* owner = GetOwner();
	CameraComponent* cameraComponent = nullptr;
	if (owner) {
		cameraComponent = owner->GetComponent<CameraComponent>();
	}

	if (cameraComponent) {
		InspectorUI::TextUnformatted("Target Camera: Owner CameraComponent");
	} else {
		InspectorUI::TextDisabled("Target Camera: None");
		InspectorUI::TextDisabled("Add CameraComponent to this GameObject.");
	}

	InspectorUI::TextDisabled("Active while the Scene view is focused.");
#endif // USE_IMGUI
}

void DebugCameraComponent::UpdateEditorCamera() {
	// Sceneビューにフォーカスがある時だけ操作する(プレイ中でもSceneを覗きながら見回せる)。
	// フォーカスが無い時(Gameビュー操作中など)は動かさず、ゲーム入力と競合させない。
	if (!IsSceneViewFocused()) {
		return;
	}

	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	if (!initialized_) {
		InitializeFromOwnerTransform();
	}

	SyncDebugCameraFromExternalTransform();
	debugCamera_.Update();
	SyncOwnerTransformFromDebugCamera();

	CameraComponent* cameraComponent = owner->GetComponent<CameraComponent>();
	if (cameraComponent) {
		cameraComponent->SyncFromOwnerTransform();
	}
}

void DebugCameraComponent::OnAfterReadJson() {
	initialized_ = false;
	InitializeFromOwnerTransform();
}

void DebugCameraComponent::InitializeFromOwnerTransform() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	const WorldTransform& transform = owner->GetTransform();
	debugCamera_.Initialize(transform.rotation_, transform.translation_);
	lastAppliedTranslation_ = transform.translation_;
	lastAppliedRotation_ = transform.rotation_;
	initialized_ = true;
}

void DebugCameraComponent::SyncDebugCameraFromExternalTransform() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	const WorldTransform& transform = owner->GetTransform();
	if (!HasTransformChangedExternally(transform.translation_, transform.rotation_)) {
		return;
	}

	debugCamera_.translation_ = transform.translation_;
	debugCamera_.rotation_ = transform.rotation_;
	lastAppliedTranslation_ = transform.translation_;
	lastAppliedRotation_ = transform.rotation_;
}

void DebugCameraComponent::SyncOwnerTransformFromDebugCamera() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	WorldTransform& transform = owner->GetTransform();
	transform.translation_ = debugCamera_.translation_;
	transform.rotation_ = debugCamera_.rotation_;
	lastAppliedTranslation_ = transform.translation_;
	lastAppliedRotation_ = transform.rotation_;
}

bool DebugCameraComponent::HasTransformChangedExternally(const Vector3& translation, const Vector3& rotation) const {
	if (!NearlyEqual(translation.x, lastAppliedTranslation_.x)) {
		return true;
	}
	if (!NearlyEqual(translation.y, lastAppliedTranslation_.y)) {
		return true;
	}
	if (!NearlyEqual(translation.z, lastAppliedTranslation_.z)) {
		return true;
	}
	if (!NearlyEqual(rotation.x, lastAppliedRotation_.x)) {
		return true;
	}
	if (!NearlyEqual(rotation.y, lastAppliedRotation_.y)) {
		return true;
	}
	if (!NearlyEqual(rotation.z, lastAppliedRotation_.z)) {
		return true;
	}

	return false;
}

} // namespace KujataEngine
