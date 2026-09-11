#include "MovementUtil.h"

#include "../3d/WorldTransform.h"
#include "../math/Quaternion.h"
#include "GameObject.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>

namespace KujataEngine {
namespace MovementUtil {

Vector3 ToCameraRelative(const Vector3& input, float cameraYaw) {
	Quaternion yawRotation = Quaternion::MakeRotateAxisAngle({0.0f, 1.0f, 0.0f}, cameraYaw);
	Vector3 result = yawRotation.RotateVector({input.x, 0.0f, input.z});
	result.y = 0.0f;
	return result;
}

float GetCameraYaw(const GameObject& gameObject, const std::string& cameraName) {
	Scene* scene = gameObject.GetScene();
	if (!scene) {
		return 0.0f;
	}

	GameObject* cameraObject = scene->FindGameObjectByName(cameraName);
	if (!cameraObject) {
		return 0.0f;
	}

	// rotation_.y を直接読むのではなく、ワールド行列の前方ベクトル(ローカル+Z)を
	// 水平面へ投影してyawを求める。親子付け・ロール・ピッチを含む任意の回転でも
	// 「画面で見えているカメラの向き」と一致する(Unityの cam.forward 方式)。
	cameraObject->UpdateWorldTransformSelfAndAncestors();
	const Matrix4x4& world = cameraObject->GetTransform().matWorld_;
	Vector3 forward = {world.m[2][0], 0.0f, world.m[2][2]};
	float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
	if (length < 1.0e-4f) {
		// ほぼ真上/真下を向いている場合は水平方向が定まらないためEulerへフォールバック。
		return cameraObject->GetTransform().rotation_.y;
	}
	return std::atan2(forward.x, forward.z);
}

Vector3 GetForward(const GameObject& gameObject) {
	return gameObject.GetTransform().GetRotationQuaternion().RotateVector({0.0f, 0.0f, 1.0f});
}

Vector3 GetRight(const GameObject& gameObject) {
	return gameObject.GetTransform().GetRotationQuaternion().RotateVector({1.0f, 0.0f, 0.0f});
}

Vector3 GetUp(const GameObject& gameObject) {
	return gameObject.GetTransform().GetRotationQuaternion().RotateVector({0.0f, 1.0f, 0.0f});
}

void MoveLocal(GameObject& gameObject, const Vector3& localOffset) {
	WorldTransform& transform = gameObject.GetTransform();
	transform.translation_ += transform.GetRotationQuaternion().RotateVector(localOffset);
}

void FaceDirection(WorldTransform& transform, const Vector3& direction, float turnSpeedRadPerSec, float deltaTime) {
	Vector3 flatDirection = {direction.x, 0.0f, direction.z};
	float length = std::sqrt(flatDirection.x * flatDirection.x + flatDirection.z * flatDirection.z);
	if (length < 1.0e-4f) {
		return;
	}

	Quaternion targetRotation = Quaternion::LookRotation(flatDirection);
	Quaternion newRotation = Quaternion::RotateTowards(transform.GetRotationQuaternion(), targetRotation, turnSpeedRadPerSec * deltaTime);
	transform.SetRotationFromQuaternion(newRotation);
}

void MoveAndFace(GameObject& gameObject, const Vector3& input, float moveSpeed, float turnSpeed, float deltaTime, const std::string& cameraName) {
	float inputLength = std::sqrt(input.x * input.x + input.z * input.z);
	if (inputLength < 0.01f) {
		return;
	}

	// スティックの倒し具合(0〜1)は速度へ反映し、方向は正規化する。
	float magnitude = std::clamp(inputLength, 0.0f, 1.0f);
	Vector3 normalizedInput = {input.x / inputLength, 0.0f, input.z / inputLength};

	float cameraYaw = GetCameraYaw(gameObject, cameraName);
	Vector3 moveDirection = ToCameraRelative(normalizedInput, cameraYaw);

	WorldTransform& transform = gameObject.GetTransform();
	transform.translation_ += moveDirection * (moveSpeed * magnitude * deltaTime);
	FaceDirection(transform, moveDirection, turnSpeed, deltaTime);
}

} // namespace MovementUtil
} // namespace KujataEngine
