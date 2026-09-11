#include "OrbitCameraComponent.h"

#include "../base/Time.h"
#include "../input/Input.h"
#include "../runtime/InspectorUI.h"
#include "../runtime/PlayState.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "../shapes/ShapeUtil.h"
#include "ColliderComponent.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

namespace KujataEngine {

namespace {

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key) || !json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

// yawを[-π, π]へ折り返す(累積で値が際限なく大きくなるのを防ぐ)。
float WrapYawAngle(float angle) {
	constexpr float pi = std::numbers::pi_v<float>;
	angle = std::fmod(angle + pi, 2.0f * pi);
	if (angle < 0.0f) {
		angle += 2.0f * pi;
	}
	return angle - pi;
}

} // namespace

namespace {
// プレイヤー設定。**シーンではなくアプリ全体の状態**なので、DLL内のstaticで保つ。
float gUserSensitivityScale = 1.0f;
bool gUserInvertY = false;
} // namespace

void OrbitCameraComponent::SetUserSensitivityScale(float scale) { gUserSensitivityScale = (scale > 0.0f) ? scale : 0.0f; }

float OrbitCameraComponent::GetUserSensitivityScale() { return gUserSensitivityScale; }

void OrbitCameraComponent::SetUserInvertY(bool invert) { gUserInvertY = invert; }

bool OrbitCameraComponent::GetUserInvertY() { return gUserInvertY; }

void OrbitCameraComponent::OnPlayStart() {
	// 配置されている向きから開始する(Editorで置いた画角を尊重)。
	GameObject* owner = GetOwner();
	if (owner) {
		yaw_ = owner->GetTransform().rotation_.y;
		pitch_ = std::clamp(owner->GetTransform().rotation_.x, pitchMin_, pitchMax_);
	}
	snapNextUpdate_ = true;
}

void OrbitCameraComponent::UpdateCutscene() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	float deltaTime = Time::GetDeltaTime();
	// **実時間ではなくスケール後の時間を使う。** ヒットストップ中はカメラも止まってほしい。
	float t = std::clamp(cutsceneBlendSpeed_ * deltaTime, 0.0f, 1.0f);

	WorldTransform& transform = owner->GetTransform();
	transform.translation_ += (cutscenePosition_ - transform.translation_) * t;

	// 注視点を向く。+Z前方の左手系なので、Yawはatan2(x, z)、Pitchは水平距離との比で出す。
	Vector3 toTarget = cutsceneLookAt_ - transform.translation_;
	float horizontal = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
	if (horizontal > 1.0e-4f || std::fabs(toTarget.y) > 1.0e-4f) {
		float targetYaw = std::atan2(toTarget.x, toTarget.z);
		float targetPitch = std::atan2(-toTarget.y, (std::max)(horizontal, 1.0e-4f));
		transform.rotation_.y = targetYaw;
		transform.rotation_.x = targetPitch;
		transform.rotation_.z = 0.0f;
	}
}

void OrbitCameraComponent::Update() {
	// **カットシーン中は通常の追従を丸ごと止める。**
	// 追従と演出を混ぜると、寄せている最中に注視点が動いて絵が落ち着かない。
	if (cutscene_) {
		UpdateCutscene();
		return;
	}
	if (!IsGamePlaying()) {
		snapNextUpdate_ = true;
		return;
	}

	GameObject* owner = GetOwner();
	if (!owner || !owner->GetScene()) {
		return;
	}

	GameObject* target = owner->GetScene()->FindGameObjectByName(targetName_);
	if (!target) {
		return;
	}

	float deltaTime = Time::GetDeltaTime();

	Vector3 pivotTarget = target->GetTransform().translation_ + Vector3{0.0f, pivotHeight_, 0.0f};

	// 注目対象が無効(非アクティブ)になっていたら外す(消える前に外すのはゲーム側の責任だが保険)。
	if (lockOnTarget_ && !lockOnTarget_->IsActiveInHierarchy()) {
		lockOnTarget_ = nullptr;
	}

	if (lockOnTarget_) {
		// --- Z注目: 右スティックは無視し、自機の背後から対象を見る向きへ寄せる ---
		Vector3 aimPoint = lockOnTarget_->GetTransform().translation_ + Vector3{0.0f, lockOnTargetHeight_, 0.0f};
		Vector3 toTarget = aimPoint - pivotTarget;
		float horizontal = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
		if (horizontal > 0.001f) {
			float desiredYaw = std::atan2(toTarget.x, toTarget.z);
			// 対象が高い/低い位置にいるぶんだけ基本ピッチから起こす/伏せる(半分だけ効かせて酔いを抑える)。
			float elevation = std::atan2(toTarget.y, horizontal);
			float desiredPitch = std::clamp(lockOnPitch_ - elevation * 0.5f, pitchMin_, pitchMax_);

			float lockT = (lockOnDamping_ > 0.0f) ? (1.0f - std::exp(-lockOnDamping_ * deltaTime)) : 1.0f;
			yaw_ = WrapYawAngle(yaw_ + WrapYawAngle(desiredYaw - yaw_) * lockT);
			pitch_ = pitch_ + (desiredPitch - pitch_) * lockT;
		}
		// 注視点を対象側へ少し寄せ、自機と対象の両方を画面に収める。
		pivotTarget = pivotTarget + toTarget * std::clamp(lockOnPivotBias_, 0.0f, 1.0f);
		recenterTimer_ = 0.0f;
	} else {
		// --- 右スティック → yaw/pitch(スティック上=見下ろしへ回り込み。Invert Yで反転) ---
		// 上下左右キーも右スティックと同じ入力として合成する。
		Vector2 stick = Input::GetRightStick();
		if (Input::GetKey(DIK_LEFT)) {
			stick.x -= 1.0f;
		}
		if (Input::GetKey(DIK_RIGHT)) {
			stick.x += 1.0f;
		}
		if (Input::GetKey(DIK_UP)) {
			stick.y += 1.0f;
		}
		if (Input::GetKey(DIK_DOWN)) {
			stick.y -= 1.0f;
		}
		stick.x = std::clamp(stick.x, -1.0f, 1.0f);
		stick.y = std::clamp(stick.y, -1.0f, 1.0f);
		// プレイヤー設定の倍率と反転をここで乗せる(シーンの値は基準として残す)。
		const float userScale = GetUserSensitivityScale();
		const bool invertY = (invertY_ != GetUserInvertY());
		yaw_ = WrapYawAngle(yaw_ + stick.x * sensitivityX_ * userScale * deltaTime);
		float pitchInput = invertY ? -stick.y : stick.y;
		pitch_ = std::clamp(pitch_ + pitchInput * sensitivityY_ * userScale * deltaTime, pitchMin_, pitchMax_);

		// --- リセンタリング: 無入力が続いたらターゲットの背後(ターゲットのyaw)へゆっくり回り込む ---
		bool stickIdle = (stick.x * stick.x + stick.y * stick.y) < 0.0025f;
		if (recenterEnabled_ && stickIdle) {
			recenterTimer_ += deltaTime;
			if (recenterTimer_ >= recenterWaitTime_) {
				float yawDifference = WrapYawAngle(target->GetTransform().rotation_.y - yaw_);
				float recenterT = 1.0f - std::exp(-recenterSpeed_ * deltaTime);
				yaw_ = WrapYawAngle(yaw_ + yawDifference * recenterT);
			}
		} else {
			recenterTimer_ = 0.0f;
		}
	}

	// --- 目標の姿勢と注視点 ---
	Quaternion desiredOrientation = Quaternion::FromEuler({pitch_, yaw_, 0.0f});

	if (snapNextUpdate_) {
		currentOrientation_ = desiredOrientation;
		currentPivot_ = pivotTarget;
		currentDistance_ = distance_;
		snapNextUpdate_ = false;
	}

	// --- 減衰(フレームレート非依存)。姿勢はSlerp、注視点は指数平滑 ---
	float rotationT = (rotationDamping_ > 0.0f) ? (1.0f - std::exp(-rotationDamping_ * deltaTime)) : 1.0f;
	float positionT = (positionDamping_ > 0.0f) ? (1.0f - std::exp(-positionDamping_ * deltaTime)) : 1.0f;
	currentOrientation_ = Quaternion::Slerp(currentOrientation_, desiredOrientation, rotationT);
	currentPivot_ = currentPivot_ + (pivotTarget - currentPivot_) * positionT;

	// --- 遮蔽回避: 引き寄せは即時(めり込み防止)、復帰は滑らか ---
	// 演出用の一時倍率を掛けた距離を基準にする(遮蔽回避はこの後に効く)。
	float desiredDistance = distance_ * distanceScale_;
	if (collisionEnabled_) {
		desiredDistance = (std::min)(desiredDistance, ComputeOccludedDistance(*owner->GetScene(), owner, target));
	}
	if (desiredDistance < currentDistance_) {
		currentDistance_ = desiredDistance;
	} else {
		float recoveryT = 1.0f - std::exp(-occlusionRecovery_ * deltaTime);
		currentDistance_ += (desiredDistance - currentDistance_) * recoveryT;
	}

	// --- カメラ位置 = 注視点から後方(姿勢の-Z方向)へdistance ---
	Vector3 backOffset = currentOrientation_.RotateVector({0.0f, 0.0f, -currentDistance_});
	WorldTransform& transform = owner->GetTransform();
	transform.translation_ = currentPivot_ + backOffset;
	transform.SetRotationFromQuaternion(currentOrientation_);
}

float OrbitCameraComponent::ComputeOccludedDistance(Scene& scene, GameObject* cameraOwner, GameObject* target) const {
	// 注視点→理想カメラ位置(フル距離)の線分をColliderと判定し、最初のヒットの手前へ引き寄せる。
	Vector3 direction = currentOrientation_.RotateVector({0.0f, 0.0f, -1.0f});
	Segment segment{};
	segment.origin = currentPivot_;
	segment.diff = direction * distance_;

	float nearestT = 1.0f;
	bool hit = false;

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || gameObject.get() == cameraOwner || gameObject.get() == target) {
			continue;
		}
		if (!gameObject->IsActiveInHierarchy()) {
			continue;
		}

		uint32_t layer = gameObject->GetLayer();
		if (layer > 31) {
			layer = 31;
		}
		if ((obstacleMask_ & (1u << layer)) == 0) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			ColliderComponent* collider = dynamic_cast<ColliderComponent*>(component.get());
			if (!collider || !collider->IsEnabled() || collider->IsTrigger()) {
				continue;
			}

			float t = 1.0f;
			bool colliderHit = false;
			if (collider->GetShapeType() == ColliderShapeType::Box) {
				BoxColliderComponent* box = dynamic_cast<BoxColliderComponent*>(collider);
				if (box && box->UsesWorldOBB()) {
					colliderHit = ShapeUtil::RaycastSegment(segment, box->GetWorldOBB(), t);
				} else {
					colliderHit = ShapeUtil::RaycastSegment(segment, collider->GetWorldAABB(), t);
				}
			} else if (collider->GetShapeType() == ColliderShapeType::Capsule) {
				// Capsuleは実形状で判定する(外接球近似だとHeightを増やしただけで誤ヒットするため)。
				CapsuleColliderComponent* capsule = dynamic_cast<CapsuleColliderComponent*>(collider);
				if (capsule) {
					colliderHit = ShapeUtil::RaycastSegment(segment, capsule->GetWorldCapsule(), t);
				} else {
					colliderHit = ShapeUtil::RaycastSegment(segment, collider->GetWorldSphere(), t);
				}
			} else {
				// Sphere。
				colliderHit = ShapeUtil::RaycastSegment(segment, collider->GetWorldSphere(), t);
			}

			if (colliderHit) {
				hit = true;
				nearestT = (std::min)(nearestT, t);
			}
		}
	}

	if (!hit) {
		return distance_;
	}

	float occludedDistance = nearestT * distance_ - cameraRadius_;
	return std::clamp(occludedDistance, minDistance_, distance_);
}

void OrbitCameraComponent::DrawInspector() {
#ifdef USE_IMGUI
	// HierarchyからGameObjectをドロップしてターゲット指定する(名前で保存)。
	void* droppedObject = nullptr;
	bool cleared = false;
	if (InspectorUI::ObjectField("Target", targetName_.c_str(), &droppedObject, &cleared)) {
		if (cleared) {
			targetName_.clear();
		} else if (droppedObject) {
			targetName_ = static_cast<GameObject*>(droppedObject)->GetName();
		}
	}

	InspectorUI::DragFloat("Distance", &distance_, 0.05f, 0.5f, 100.0f);
	InspectorUI::DragFloat("Pivot Height", &pivotHeight_, 0.05f);
	InspectorUI::DragFloat("Sensitivity X", &sensitivityX_, 0.05f, 0.0f, 20.0f);
	InspectorUI::DragFloat("Sensitivity Y", &sensitivityY_, 0.05f, 0.0f, 20.0f);
	InspectorUI::Checkbox("Invert Y", &invertY_);
	InspectorUI::DragFloat("Pitch Min", &pitchMin_, 0.01f, -1.55f, 1.55f);
	InspectorUI::DragFloat("Pitch Max", &pitchMax_, 0.01f, -1.55f, 1.55f);
	InspectorUI::DragFloat("Position Damping", &positionDamping_, 0.1f, 0.0f, 100.0f);
	InspectorUI::DragFloat("Rotation Damping", &rotationDamping_, 0.1f, 0.0f, 100.0f);

	if (pitchMax_ < pitchMin_) {
		pitchMax_ = pitchMin_;
	}

	// --- 遮蔽回避 ---
	InspectorUI::TextUnformatted("Collision");
	InspectorUI::Checkbox("Enable Collision", &collisionEnabled_);
	InspectorUI::DragFloat("Camera Radius", &cameraRadius_, 0.01f, 0.0f, 5.0f);
	InspectorUI::DragFloat("Min Distance", &minDistance_, 0.05f, 0.1f, 50.0f);
	InspectorUI::DragFloat("Occlusion Recovery", &occlusionRecovery_, 0.1f, 0.0f, 50.0f);

	int obstacleMask = -1;
	if (obstacleMask_ != 0xffffffff) {
		obstacleMask = static_cast<int>(obstacleMask_);
	}
	if (InspectorUI::DragInt("Obstacle Mask", &obstacleMask, 1.0f, -1, 0x7fffffff)) {
		if (obstacleMask < 0) {
			obstacleMask_ = 0xffffffff;
		} else {
			obstacleMask_ = static_cast<uint32_t>(obstacleMask);
		}
	}

	// --- リセンタリング ---
	InspectorUI::TextUnformatted("Recenter");
	InspectorUI::Checkbox("Enable Recenter", &recenterEnabled_);
	InspectorUI::DragFloat("Recenter Wait", &recenterWaitTime_, 0.05f, 0.0f, 30.0f);
	InspectorUI::DragFloat("Recenter Speed", &recenterSpeed_, 0.05f, 0.0f, 30.0f);

	// --- Z注目 ---
	InspectorUI::TextUnformatted("Lock On");
	InspectorUI::DragFloat("Lock On Pitch", &lockOnPitch_, 0.01f, -1.55f, 1.55f);
	InspectorUI::DragFloat("Lock On Damping", &lockOnDamping_, 0.1f, 0.0f, 100.0f);
	InspectorUI::DragFloat("Lock On Pivot Bias", &lockOnPivotBias_, 0.01f, 0.0f, 1.0f);
	InspectorUI::TextUnformatted(lockOnTarget_ ? ("Target: " + lockOnTarget_->GetName()).c_str() : "Target: (none)");
#endif // USE_IMGUI
}

void OrbitCameraComponent::WriteJson(nlohmann::json& json) const {
	json["targetName"] = targetName_;
	json["distance"] = distance_;
	json["pivotHeight"] = pivotHeight_;
	json["sensitivityX"] = sensitivityX_;
	json["sensitivityY"] = sensitivityY_;
	json["invertY"] = invertY_;
	json["pitchMin"] = pitchMin_;
	json["pitchMax"] = pitchMax_;
	json["positionDamping"] = positionDamping_;
	json["rotationDamping"] = rotationDamping_;
	json["collisionEnabled"] = collisionEnabled_;
	json["cameraRadius"] = cameraRadius_;
	json["minDistance"] = minDistance_;
	json["occlusionRecovery"] = occlusionRecovery_;
	json["obstacleMask"] = obstacleMask_;
	json["recenterEnabled"] = recenterEnabled_;
	json["recenterWaitTime"] = recenterWaitTime_;
	json["recenterSpeed"] = recenterSpeed_;
	json["lockOnPitch"] = lockOnPitch_;
	json["lockOnDamping"] = lockOnDamping_;
	json["lockOnPivotBias"] = lockOnPivotBias_;
}

void OrbitCameraComponent::ReadJson(const nlohmann::json& json) {
	if (json.contains("targetName") && json.at("targetName").is_string()) {
		targetName_ = json.at("targetName").get<std::string>();
	}
	distance_ = ReadFloat(json, "distance", distance_);
	pivotHeight_ = ReadFloat(json, "pivotHeight", pivotHeight_);
	sensitivityX_ = ReadFloat(json, "sensitivityX", sensitivityX_);
	sensitivityY_ = ReadFloat(json, "sensitivityY", sensitivityY_);
	if (json.contains("invertY") && json.at("invertY").is_boolean()) {
		invertY_ = json.at("invertY").get<bool>();
	}
	pitchMin_ = ReadFloat(json, "pitchMin", pitchMin_);
	pitchMax_ = ReadFloat(json, "pitchMax", pitchMax_);
	positionDamping_ = ReadFloat(json, "positionDamping", positionDamping_);
	rotationDamping_ = ReadFloat(json, "rotationDamping", rotationDamping_);

	if (json.contains("collisionEnabled") && json.at("collisionEnabled").is_boolean()) {
		collisionEnabled_ = json.at("collisionEnabled").get<bool>();
	}
	cameraRadius_ = ReadFloat(json, "cameraRadius", cameraRadius_);
	minDistance_ = ReadFloat(json, "minDistance", minDistance_);
	occlusionRecovery_ = ReadFloat(json, "occlusionRecovery", occlusionRecovery_);
	if (json.contains("obstacleMask") && (json.at("obstacleMask").is_number_unsigned() || json.at("obstacleMask").is_number_integer())) {
		int64_t maskValue = json.at("obstacleMask").get<int64_t>();
		if (maskValue >= 0) {
			obstacleMask_ = static_cast<uint32_t>(maskValue);
		}
	}
	if (json.contains("recenterEnabled") && json.at("recenterEnabled").is_boolean()) {
		recenterEnabled_ = json.at("recenterEnabled").get<bool>();
	}
	recenterWaitTime_ = ReadFloat(json, "recenterWaitTime", recenterWaitTime_);
	recenterSpeed_ = ReadFloat(json, "recenterSpeed", recenterSpeed_);
	lockOnPitch_ = ReadFloat(json, "lockOnPitch", lockOnPitch_);
	lockOnDamping_ = ReadFloat(json, "lockOnDamping", lockOnDamping_);
	lockOnPivotBias_ = ReadFloat(json, "lockOnPivotBias", lockOnPivotBias_);
}

} // namespace KujataEngine
