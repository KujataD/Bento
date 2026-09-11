#include "CriticalStrikeComponent.h"
#include "GameEvents.h"
#include "GameAudio.h"

#include "CharacterMotor.h"
#include "EnemyHealth.h"
#include "IAbilitySet.h"
#include "IEnemy.h"
#include "PlayerHealth.h"
#include "Player.h"

#include <Editor/PrefabAsset.h>
#include <components/ImageComponent.h>
#include <components/OrbitCameraComponent.h>
#include <scene/MovementUtil.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

void CriticalStrikeComponent::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	health_ = GetComponent<PlayerHealth>();
	animator_ = GetComponentInChildren<AnimatorComponent>();
	abilitySet_ = GetComponent<IAbilitySet>();
	phase_ = Phase::Idle;
	phaseTimer_ = 0.0f;
	target_ = nullptr;
	promptTarget_ = nullptr;
	// プロンプトはPlayインスタンスごとに作り直す(前回Playのポインタは無効)。
	prompt_ = nullptr;
	promptTried_ = false;
	cutsceneElapsed_ = 0.0f;
	// 前回Playが途中で止まっていても、時間とカメラは必ず戻す。
	Restore();
}

void CriticalStrikeComponent::OnPlayStop() {
	Restore();
}

void CriticalStrikeComponent::Update() {
	// **実時間で数える。** ヒットストップ中はGetDeltaTimeが0になるため、
	// スケールした時間で数えると自分のタイマーが止まって永久に復帰できない。
	float deltaTime = Time::GetUnscaledDeltaTime();

	if (phase_ != Phase::Idle) {
		cutsceneElapsed_ += deltaTime;
		// 実行中はバナーを消す(自分が決めている最中に案内は要らない)。
		UpdatePrompt(nullptr);
		phaseTimer_ -= deltaTime;
		if (phaseTimer_ > 0.0f) {
			// カットシーンの画は演出中ずっと作り続ける。**振りかぶりから余韻まで途切れさせない**。
			UpdateCutsceneShot(CutsceneProgress());
			// 溜めの見せ方は技side任せ(術師は杖の球を育てる)。剣士は何もしない。
			if (phase_ == Phase::Windup && abilitySet_ && windupSeconds_ > 0.0f) {
				abilitySet_->OnCriticalWindup(std::clamp(1.0f - phaseTimer_ / windupSeconds_, 0.0f, 1.0f));
			}
			return;
		}

		switch (phase_) {
		case Phase::Windup:
			Impact();
			return;
		case Phase::Hitstop:
			// 時間を元へ戻し、見せ場へ。**カットシーンはここからが本番**。
			Time::SetTimeScale(1.0f);
			phase_ = Phase::Aftermath;
			phaseTimer_ = aftermathSeconds_;
			return;
		case Phase::Aftermath:
			phase_ = Phase::Recover;
			phaseTimer_ = recoverSeconds_;
			return;
		case Phase::Recover:
			Restore();
			phase_ = Phase::Idle;
			target_ = nullptr;
			return;
		default:
			break;
		}
		return;
	}

	// --- 待機中: プロンプトの相手を探して出しておく ---
	// **発動の入力はここでは読まない。** 致命は通常攻撃と同じR2/Kで出すので、
	// 入力を読むのは Player::Update 1か所に寄せてある(両方で読むと1回の押下で二重に発火する)。
	// ここは「誰に出せるか」を GetPromptTarget() で公開するところまでを受け持つ。
	// 表示は広め(Prompt Show Distance)、発動は狭め(Trigger Distance)。
	// 遠いうちからバナーを出して「あそこへ行けば決められる」と分からせるのが狙い。
	// **操作中のキャラ以外は何もしない。**
	// このコンポーネントは2人とも持っているので、ここを見ないと1回の押下で相方の致命まで
	// 同時に発動する(剣士で押したのに術師の致命が出る、という形で表面化する)。
	// バナーも二重に出てしまうため、入力だけでなく表示ごとここで止める。
	// リーダーの印はPartyManagerがPlayerコンポーネントの有効/無効で付けているので、それに合わせる。
	if (!IsPlayerControlled()) {
		UpdatePrompt(nullptr);
		promptTarget_ = nullptr;
		return;
	}

	GameObject* visible = FindStaggeredTarget();
	UpdatePrompt(visible);
	promptTarget_ = nullptr;
	if (!visible || !owner_) {
		return;
	}

	Vector3 diff = visible->GetTransform().translation_ - owner_->GetTransform().translation_;
	diff.y = 0.0f;
	if (std::sqrt(diff.x * diff.x + diff.z * diff.z) > triggerDistance_) {
		// まだ届いていない。バナーは出ているが発動はできない。
		return;
	}
	promptTarget_ = visible;
}

bool CriticalStrikeComponent::IsPlayerControlled() const {
	// Playerが付いていない構成(AI専用キャラなど)では致命を出させない。
	const Player* inputBrain = GetComponent<Player>();
	return inputBrain && inputBrain->IsEnabled();
}

GameObject* CriticalStrikeComponent::FindStaggeredTarget() const {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	const Vector3 selfPosition = owner_->GetTransform().translation_;
	GameObject* nearest = nullptr;
	float nearestDistanceSq = 0.0f;

	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy()) {
			continue;
		}
		// 敵であること(IEnemy)と、体勢を崩してスタンしていること。この2つが「窓」。
		IEnemy* enemy = object->GetComponent<IEnemy>();
		if (!enemy || !enemy->IsTargetable()) {
			continue;
		}
		EnemyHealth* health = object->GetComponent<EnemyHealth>();
		if (!health || !health->IsStaggered()) {
			continue;
		}

		Vector3 diff = object->GetTransform().translation_ - selfPosition;
		diff.y = 0.0f;
		float distanceSq = diff.x * diff.x + diff.z * diff.z;
		if (distanceSq > promptShowDistance_ * promptShowDistance_) {
			continue;
		}
		if (!nearest || distanceSq < nearestDistanceSq) {
			nearest = object.get();
			nearestDistanceSq = distanceSq;
		}
	}
	return nearest;
}

bool CriticalStrikeComponent::TryExecute(GameObject* target) {
	if (IsExecuting() || !owner_) {
		return false;
	}
	if (!target) {
		target = FindStaggeredTarget();
	}
	if (!target) {
		return false;
	}

	// スタンが解けていたら窓は閉じている(AIが1フレーム遅れて呼ぶ場合の保険)。
	EnemyHealth* targetHealth = target->GetComponent<EnemyHealth>();
	if (!targetHealth || !targetHealth->IsStaggered()) {
		return false;
	}

	Vector3 diff = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	diff.y = 0.0f;
	if (std::sqrt(diff.x * diff.x + diff.z * diff.z) > triggerDistance_) {
		return false;
	}
	if (motor_ && motor_->IsActionLocked()) {
		return false;
	}

	Begin(target);
	return true;
}

void CriticalStrikeComponent::Begin(GameObject* target) {
	if (!owner_ || !target) {
		return;
	}

	target_ = target;
	phase_ = Phase::Windup;
	phaseTimer_ = windupSeconds_;

	// チュートリアルの課題判定用。**操作中のキャラのぶんだけ数える**
	// (AI相方が決めたぶんで課題が終わってしまわないように)。
	if (IsPlayerControlled()) {
		++GameEvents::CriticalCountRef();
	}

	// 相手の方を向き、決めた距離まで吸い付く。離れた位置から始まると空振りに見えるため。
	WorldTransform& transform = owner_->GetTransform();
	Vector3 toTarget = target->GetTransform().translation_ - transform.translation_;
	toTarget.y = 0.0f;
	float distance = Length(toTarget);
	if (distance > 0.0001f) {
		Vector3 direction = toTarget / distance;
		MovementUtil::FaceDirection(transform, direction, 1000.0f, 1.0f);
		if (distance > strikeOffset_) {
			transform.translation_ = target->GetTransform().translation_ - direction * strikeOffset_;
			transform.translation_.y = owner_->GetTransform().translation_.y;
		}
	}

	// 出し切りの保証: 演出が終わるまで動けず、かつ無敵。
	// 「決めに行ったのに横槍で潰された」が起きないことが、思い切って踏み込める理由になる。
	//
	// **見せ場(Aftermath)のぶんまで必ず含める。** ここを足し忘れると、カメラがまだ寄っている
	// 最中に行動ロックだけ先に切れ、演出の画のまま操作キャラが走り出す。
	// 拍の合計 = 振りかぶり + ヒットストップ + 見せ場 + 硬直。Updateの遷移と同じ順で足すこと。
	if (motor_) {
		motor_->BeginActionLock(windupSeconds_ + hitstopSeconds_ + aftermathSeconds_ + recoverSeconds_);
	}
	if (health_) {
		health_->SetInvincible(true);
	}
	if (animator_ && !criticalClipName_.empty()) {
		animator_->PlayByName(criticalClipName_.c_str());
	}

	// **カメラを預かる。** ここから Restore() まで、追従も右スティックも効かない。
	if (KujataEngine::OrbitCameraComponent* camera = FindCamera()) {
		camera->BeginCutscene(cameraBlendSpeed_);
	}
	cutsceneElapsed_ = 0.0f;
	UpdateCutsceneShot(0.0f);
}

void CriticalStrikeComponent::Impact() {
	GameAudio::PlaySe(GameAudio::Se::Critical);
	// 相手が消えていても、演出だけは最後まで通す(途中で固まらないため)。
	// **決め方は技側が知っている。** 技が自前で決めたなら(true)、こちらは素のダメージを出さない。
	bool handledByAbility = abilitySet_ && abilitySet_->TryCritical(target_, damage_);
	if (target_) {
		if (EnemyHealth* health = target_->GetComponent<EnemyHealth>()) {
			// **相手を大きく仰け反らせる。** 決めた手応えは、こちらの動きより相手の反応で伝わる。
			health->NotifyCritical(aftermathSeconds_ + recoverSeconds_);
			// 体勢崩しは使い切る。スタンを終わらせて「決めた」区切りを付ける。
			health->ClearStagger();
			if (!handledByAbility) {
				health->TakeDamage(damage_, 0.0f, owner_);
			}
		}
	}

	// **打撃の重さはここで決まる。** 当たった瞬間に時間を止め、目と手に衝撃を伝える。
	Time::SetTimeScale(hitstopTimeScale_);
	phase_ = Phase::Hitstop;
	phaseTimer_ = hitstopSeconds_;
}

void CriticalStrikeComponent::Restore() {
	if (abilitySet_) {
		abilitySet_->OnCriticalEnd();
	}
	if (prompt_) {
		prompt_->SetActive(false);
	}
	// 一時的に変えたものは全てここへ集約する(中断・終了・Play停止のどこからでも通る)。
	Time::SetTimeScale(1.0f);
	// **カメラは必ず返す。** 返し忘れると視点が固まったまま操作不能に見える。
	if (KujataEngine::OrbitCameraComponent* camera = FindCamera()) {
		camera->EndCutscene();
	}
	SetCameraDistanceScale(1.0f);
	if (health_) {
		health_->SetInvincible(false);
	}
}

KujataEngine::OrbitCameraComponent* CriticalStrikeComponent::FindCamera() const {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (KujataEngine::OrbitCameraComponent* camera = object->GetComponent<KujataEngine::OrbitCameraComponent>()) {
			return camera;
		}
	}
	return nullptr;
}

float CriticalStrikeComponent::CutsceneProgress() const {
	float total = windupSeconds_ + hitstopSeconds_ + aftermathSeconds_ + recoverSeconds_;
	return std::clamp(cutsceneElapsed_ / (std::max)(total, 1.0e-3f), 0.0f, 1.0f);
}

void CriticalStrikeComponent::UpdateCutsceneShot(float progress) {
	KujataEngine::OrbitCameraComponent* camera = FindCamera();
	if (!camera || !owner_) {
		return;
	}

	Vector3 selfPosition = owner_->GetTransform().translation_;
	// 相手は狙い点(胸のあたり)を使う。足元を基準にすると、巨体では画面下に寄りすぎる。
	Vector3 targetPosition = selfPosition;
	if (target_) {
		targetPosition = target_->GetTransform().translation_;
		if (IEnemy* enemy = target_->GetComponent<IEnemy>()) {
			targetPosition = enemy->GetLockOnPoint();
		}
	}

	// **2人の中点を挟んで横から捉える。** 背後からの通常視点と画が被らないので、
	// 切り替わった瞬間に「別のカメラになった」と伝わる。
	Vector3 mid = (selfPosition + targetPosition) * 0.5f;
	Vector3 axis = targetPosition - selfPosition;
	axis.y = 0.0f;
	float axisLength = std::sqrt(axis.x * axis.x + axis.z * axis.z);
	if (axisLength < 1.0e-4f) {
		axis = {0.0f, 0.0f, 1.0f};
	} else {
		axis = axis / axisLength;
	}

	// 進行に合わせて横位置を回り込ませる。止まった絵にしないための肝。
	float orbit = shotOrbitDeg_ * (std::numbers::pi_v<float> / 180.0f) * progress;
	Vector3 side = {axis.z, 0.0f, -axis.x};
	Vector3 orbited = {
	    side.x * std::cos(orbit) + axis.x * std::sin(orbit),
	    0.0f,
	    side.z * std::cos(orbit) + axis.z * std::sin(orbit),
	};

	Vector3 shot = mid + orbited * shotSideOffset_ - axis * shotBackOffset_;
	shot.y = mid.y + shotHeight_;
	camera->SetCutsceneShot(shot, mid);
}

GameObject* CriticalStrikeComponent::AcquirePrompt() {
	if (prompt_ || promptTried_) {
		return prompt_;
	}
	promptTried_ = true;

	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || promptPrefabPath_.empty()) {
		return nullptr;
	}
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, promptPrefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[CriticalStrikeComponent] prompt prefab load failed (" + promptPrefabPath_ + "): " + result.message);
		return nullptr;
	}
	prompt_ = result.rootObject;
	prompt_->SetActive(false);
	return prompt_;
}

void CriticalStrikeComponent::UpdatePrompt(GameObject* target) {
	GameObject* prompt = AcquirePrompt();
	if (!prompt) {
		return;
	}

	if (!target || !owner_) {
		if (prompt->IsActive()) {
			prompt->SetActive(false);
		}
		return;
	}

	// 位置: 敵の狙い点(IEnemyが決める)のさらに上。
	Vector3 anchor = target->GetTransform().translation_;
	if (IEnemy* enemy = target->GetComponent<IEnemy>()) {
		anchor = enemy->GetLockOnPoint();
	}
	anchor.y += promptHeight_;

	WorldTransform& transform = prompt->GetTransform();
	transform.translation_ = anchor;

	// World Spaceのキャンバスは自分ではカメラを向かないので、Yawだけこちらで向ける。
	Vector3 toSelf = owner_->GetTransform().translation_ - anchor;
	if (std::fabs(toSelf.x) > 0.0001f || std::fabs(toSelf.z) > 0.0001f) {
		transform.rotation_.y = std::atan2(toSelf.x, toSelf.z);
	}

	// **届いていれば光り、遠ければ暗い。** これが「今だ」の合図になる。
	Vector3 flat = {toSelf.x, 0.0f, toSelf.z};
	bool inRange = std::sqrt(flat.x * flat.x + flat.z * flat.z) <= triggerDistance_;
	for (GameObject* child : prompt->GetChildren()) {
		if (!child) {
			continue;
		}
		if (ImageComponent* image = child->GetComponent<ImageComponent>()) {
			image->SetColor(inRange ? Vector4{1.0f, 0.25f, 0.15f, 0.85f} : Vector4{0.35f, 0.1f, 0.1f, 0.35f});
		}
	}

	prompt->SetActive(true);
}

void CriticalStrikeComponent::SetCameraDistanceScale(float scale) {
	if (!owner_ || !owner_->GetScene()) {
		return;
	}
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (OrbitCameraComponent* camera = object->GetComponent<OrbitCameraComponent>()) {
			camera->SetDistanceScale(scale);
			return;
		}
	}
}
