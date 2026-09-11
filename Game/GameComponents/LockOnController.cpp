#include "LockOnController.h"
#include "GameAudio.h"
#include "GameEvents.h"
#include "CharacterMotor.h"
#include "GameInput.h"
#include "IEnemy.h"
#include "PartyManager.h"

#include <Editor/PrefabAsset.h>
#include <components/OrbitCameraComponent.h>
#include <scene/MovementUtil.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace KujataEngine;

namespace {

float HorizontalDistance(const Vector3& a, const Vector3& b) {
	float dx = a.x - b.x;
	float dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

} // namespace

void LockOnController::OnPlayStart() {
	target_ = nullptr;
	appliedLeader_ = nullptr;
	stickHeld_ = false;
	// レティクルはPlayインスタンスごとに作り直す(前回Playのポインタは無効)。
	reticle_ = nullptr;
	reticleTried_ = false;
}

void LockOnController::Update() {
	GameObject* leader = FindLeader();

	// --- 対象の有効性(死亡・消滅・距離)を毎フレーム確認し、ダメなら外す ---
	if (target_ && !IsValidTarget(target_, leader, releaseDistance_)) {
		target_ = nullptr;
	}

	// --- 入力: R3/Qでトグル、注目中は右スティックを倒した瞬間で切替 ---
	if (GameInput::IsLockOnTriggered()) {
		Toggle();
	}
	if (target_) {
		float stickX = Input::GetRightStick().x;
		bool held = std::fabs(stickX) >= switchStickThreshold_;
		if (held && !stickHeld_) {
			SwitchTarget(stickX > 0.0f ? 1 : -1);
		}
		stickHeld_ = held;
	} else {
		stickHeld_ = false;
	}

	ApplyToLeaderAndCamera();
	UpdateReticle();
}

void LockOnController::Toggle() {
	if (target_) {
		Clear();
		return;
	}

	std::vector<Candidate> candidates;
	CollectCandidates(candidates);
	if (candidates.empty()) {
		// **押したのに何も起きない**が一番分かりにくいので、理由を掲示する([[GameEvents]])。
		GameEvents::ReportFailure(GameEvents::Failure::NoLockOnTarget);
		return;
	}

	// 画面中央(カメラ正面)に近い敵を優先し、同程度なら近い方。
	const Candidate* best = nullptr;
	float bestScore = 0.0f;
	for (const Candidate& candidate : candidates) {
		float score = std::fabs(candidate.signedAngle) + (candidate.distance / std::max(lockDistance_, 0.001f)) * 0.3f;
		if (!best || score < bestScore) {
			best = &candidate;
			bestScore = score;
		}
	}
	target_ = best ? best->object : nullptr;
	if (target_) {
		++GameEvents::LockOnCountRef(); // チュートリアルの課題判定用。
		GameAudio::PlaySe(GameAudio::Se::LockOn);
	}
}

void LockOnController::Clear() {
	// **掴んでいたときだけ鳴らす。** 何も掴んでいない状態でも Clear は通るので、
	// 無条件に鳴らすと解除音だけが空撃ちで鳴る。
	if (target_) {
		GameAudio::PlaySe(GameAudio::Se::LockOff);
	}
	target_ = nullptr;
}

void LockOnController::SwitchTarget(int direction) {
	if (!target_ || direction == 0) {
		return;
	}

	std::vector<Candidate> candidates;
	CollectCandidates(candidates);

	// 現在の対象の角度を基準に、指定方向で最も近い角度の敵へ。
	float currentAngle = 0.0f;
	bool found = false;
	for (const Candidate& candidate : candidates) {
		if (candidate.object == target_) {
			currentAngle = candidate.signedAngle;
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}

	float minAngle = switchMinAngle_ * std::numbers::pi_v<float> / 180.0f;
	const Candidate* best = nullptr;
	float bestDelta = 0.0f;
	for (const Candidate& candidate : candidates) {
		if (candidate.object == target_) {
			continue;
		}
		float delta = (candidate.signedAngle - currentAngle) * static_cast<float>(direction);
		if (delta < minAngle) {
			continue;
		}
		if (!best || delta < bestDelta) {
			best = &candidate;
			bestDelta = delta;
		}
	}
	if (best) {
		target_ = best->object;
	}
}

Vector3 LockOnController::GetTargetPoint() const {
	if (!target_) {
		return {0.0f, 0.0f, 0.0f};
	}
	// 狙い点は敵側が決める(IEnemy)。敵ごとにInspectorで自由に設定できる。
	if (IEnemy* enemy = target_->GetComponent<IEnemy>()) {
		return enemy->GetLockOnPoint();
	}
	// IEnemyを外した個体への保険。足元だと地面に埋まるので少しだけ持ち上げる。
	return target_->GetTransform().translation_ + Vector3{0.0f, 1.0f, 0.0f};
}

LockOnController* LockOnController::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (LockOnController* controller = object->GetComponent<LockOnController>()) {
			return controller;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

GameObject* LockOnController::FindLeader() const {
	if (!owner_) {
		return nullptr;
	}
	// 同じGameObjectのPartyManagerを優先(通常はここに置く)。無ければシーンから探す。
	if (PartyManager* manager = owner_->GetComponent<PartyManager>()) {
		if (manager->GetLeader()) {
			return manager->GetLeader();
		}
	}
	return PartyManager::FindLeaderInScene(owner_->GetScene(), "");
}

GameObject* LockOnController::FindCamera() const {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}
	return owner_->GetScene()->FindGameObjectByName(cameraName_);
}

bool LockOnController::IsValidTarget(GameObject* target, GameObject* leader, float maxDistance) const {
	if (!target || !owner_ || !owner_->GetScene()) {
		return false;
	}

	// シーンにまだ存在するか(消滅したオブジェクトのポインタを触らない)。
	bool exists = false;
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (object.get() == target) {
			exists = true;
			break;
		}
	}
	if (!exists || !target->IsActiveInHierarchy()) {
		return false;
	}

	if (!IsTargetableObject(target)) {
		return false;
	}

	if (leader && HorizontalDistance(leader->GetTransform().translation_, target->GetTransform().translation_) > maxDistance) {
		return false;
	}
	return true;
}

bool LockOnController::IsTargetableObject(GameObject* object) const {
	if (!object) {
		return false;
	}

	// **狙えるのは敵(IEnemy)だけ。** 味方は倒れていても候補にしない。
	// 蘇生は倒れてからの経過時間で自力に進むので、相方へ狙いを定める理由が無い
	// ([[death-and-revive]])。候補に混ぜると、戦闘中の切替で味方を掴んでしまう。
	if (IEnemy* enemy = object->GetComponent<IEnemy>()) {
		return enemy->IsTargetable();
	}
	return false;
}

void LockOnController::CollectCandidates(std::vector<Candidate>& outCandidates) const {
	outCandidates.clear();
	GameObject* leader = FindLeader();
	if (!leader || !owner_ || !owner_->GetScene()) {
		return;
	}

	// カメラ正面(水平)と右方向。カメラが無ければリーダーの向きで代用する。
	float cameraYaw = MovementUtil::GetCameraYaw(*leader, cameraName_);
	Vector3 forward = {std::sin(cameraYaw), 0.0f, std::cos(cameraYaw)};
	Vector3 right = {std::cos(cameraYaw), 0.0f, -std::sin(cameraYaw)};

	const Vector3& origin = leader->GetTransform().translation_;
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy()) {
			continue;
		}
		if (!IsTargetableObject(object.get())) {
			continue;
		}
		Vector3 toEnemy = object->GetTransform().translation_ - origin;
		toEnemy.y = 0.0f;
		float distance = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
		if (distance > lockDistance_ || distance < 0.0001f) {
			continue;
		}
		float forwardDot = toEnemy.x * forward.x + toEnemy.z * forward.z;
		float rightDot = toEnemy.x * right.x + toEnemy.z * right.z;

		Candidate candidate;
		candidate.object = object.get();
		candidate.signedAngle = std::atan2(rightDot, forwardDot);
		candidate.distance = distance;
		outCandidates.push_back(candidate);
	}
}

void LockOnController::ApplyToLeaderAndCamera() {
	GameObject* leader = FindLeader();

	// リーダーが替わっていたら、前のリーダーのストレイフ設定を外す。
	if (appliedLeader_ && appliedLeader_ != leader) {
		if (CharacterMotor* motor = appliedLeader_->GetComponent<CharacterMotor>()) {
			motor->SetFacingTarget(nullptr);
		}
	}
	appliedLeader_ = leader;

	if (leader) {
		if (CharacterMotor* motor = leader->GetComponent<CharacterMotor>()) {
			motor->SetFacingTarget(target_);
		}
	}

	if (GameObject* cameraObject = FindCamera()) {
		if (OrbitCameraComponent* camera = cameraObject->GetComponent<OrbitCameraComponent>()) {
			if (target_) {
				// カメラへは「対象位置からの高さ」で渡す規約なので、狙い点との差分に直す。
				float height = GetTargetPoint().y - target_->GetTransform().translation_.y;
				camera->SetLockOnTarget(target_, height);
			} else {
				camera->ClearLockOnTarget();
			}
		}
	}
}

void LockOnController::UpdateReticle() {
	if (!target_) {
		if (reticle_) {
			reticle_->SetActive(false);
		}
		return;
	}

	if (!reticle_ && !reticleTried_) {
		reticleTried_ = true;
		Scene* scene = owner_ ? owner_->GetScene() : nullptr;
		if (scene && !reticlePrefabPath_.empty()) {
			PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, reticlePrefabPath_, false);
			if (result.succeeded && result.rootObject) {
				reticle_ = result.rootObject;
			} else {
				Logger::Log("[LockOnController] reticle prefab load failed (" + reticlePrefabPath_ + "): " + result.message);
			}
		}
	}
	if (!reticle_) {
		return;
	}

	reticle_->SetActive(true);
	reticle_->GetTransform().translation_ = GetTargetPoint();
}
