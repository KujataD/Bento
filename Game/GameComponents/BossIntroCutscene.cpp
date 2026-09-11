#include "BossIntroCutscene.h"

#include "AllyAIBrain.h"
#include "GuardianBossComponent.h"
#include "PartyManager.h"
#include "Player.h"

#include <components/ImageComponent.h>
#include <components/OrbitCameraComponent.h>
#include <components/TextComponent.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

namespace {

/// <summary>0→1を、始めと終わりがゆるやかな曲線へ均す。**等速で動かすと機械に見える。**</summary>
float SmoothStep(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/// <summary>飛ばす操作。特定のボタンに限らず「何か押した」で拾う。</summary>
bool IsSkipTriggered() {
	return Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_A) || Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_B) ||
	       Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_START) || Input::GetKeyTrigger(DIK_SPACE) ||
	       Input::GetKeyTrigger(DIK_RETURN) || Input::GetKeyTrigger(DIK_ESCAPE);
}

} // namespace

void BossIntroCutscene::OnPlayStart() {
	// Playインスタンスは使い回されるので、非シリアライズの状態は必ず戻す。
	boss_ = nullptr;
	suspendedBrains_.clear();
	actorsSuspended_ = false;
	timer_ = 0.0f;
	revealEndPosition_ = {0.0f, 0.0f, 0.0f};
	revealEndLookAt_ = {0.0f, 0.0f, 0.0f};

	phase_ = Phase::Reveal;

	// 名前とHPバーは伏せて始める。**HPバーは演出が終わってから出す**ことで、
	// 「ここから戦いが始まる」という区切りになる。
	SetObjectActive(nameObjectName_, false);
	SetObjectActive(bossHudName_, false);
}

void BossIntroCutscene::OnPlayStop() {
	// **預かったものは必ず返す。** 途中で止められてもカメラと操作が固まったままにならないように。
	if (phase_ != Phase::Done) {
		Finish();
	}
}

BossIntroCutscene* BossIntroCutscene::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (BossIntroCutscene* intro = object->GetComponent<BossIntroCutscene>()) {
			return intro;
		}
	}
	return nullptr;
}

bool BossIntroCutscene::IsSceneIntroPlaying(Scene* scene) {
	BossIntroCutscene* intro = FindInScene(scene);
	return intro && intro->IsPlaying();
}

GameObject* BossIntroCutscene::FindBoss() {
	if (boss_) {
		return boss_;
	}
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || bossName_.empty()) {
		return nullptr;
	}
	boss_ = scene->FindGameObjectByName(bossName_);
	return boss_;
}

OrbitCameraComponent* BossIntroCutscene::FindCamera() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (OrbitCameraComponent* camera = object->GetComponent<OrbitCameraComponent>()) {
			return camera;
		}
	}
	return nullptr;
}

TextComponent* BossIntroCutscene::FindNameText() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || nameTextName_.empty()) {
		return nullptr;
	}
	GameObject* object = scene->FindGameObjectByName(nameTextName_);
	return object ? object->GetComponent<TextComponent>() : nullptr;
}

void BossIntroCutscene::ApplyNameAlpha(float alpha) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return;
	}
	if (TextComponent* label = FindNameText()) {
		Vector4 color = label->GetColor();
		color.w = alpha;
		label->SetColor(color);
	}
	// **帯も一緒に動かす。** 文字だけ薄れると、帯だけが唐突に出入りして見える。
	if (!nameBgName_.empty()) {
		if (GameObject* object = scene->FindGameObjectByName(nameBgName_)) {
			if (ImageComponent* image = object->GetComponent<ImageComponent>()) {
				Vector4 color = image->GetColor();
				if (!backdropBaseCaptured_) {
					backdropBaseAlpha_ = color.w;
					backdropBaseCaptured_ = true;
				}
				// 帯は最初から半透明なので、**元の濃さを上限にして**掛ける。
				color.w = backdropBaseAlpha_ * alpha;
				image->SetColor(color);
			}
		}
	}
}

void BossIntroCutscene::SetObjectActive(const std::string& name, bool active) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || name.empty()) {
		return;
	}
	if (GameObject* object = scene->FindGameObjectByName(name)) {
		object->SetActive(active);
	}
}

Vector3 BossIntroCutscene::BossPosition() {
	GameObject* boss = FindBoss();
	return boss ? boss->GetTransform().translation_ : Vector3{0.0f, 0.0f, 0.0f};
}

Vector3 BossIntroCutscene::LeaderPosition() {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	GameObject* leader = PartyManager::FindLeaderInScene(scene, "");
	return leader ? leader->GetTransform().translation_ : Vector3{0.0f, 0.0f, 0.0f};
}

Vector3 BossIntroCutscene::FrontDirection() {
	Vector3 toParty = LeaderPosition() - BossPosition();
	toParty.y = 0.0f;
	const float length = std::sqrt(toParty.x * toParty.x + toParty.z * toParty.z);
	if (length < 0.0001f) {
		// 二人がボスと同じ地点に居る(あり得ないが保険)。手前を-Zとみなす。
		return {0.0f, 0.0f, -1.0f};
	}
	return {toParty.x / length, 0.0f, toParty.z / length};
}

void BossIntroCutscene::SuspendActors(bool suspend) {
	if (suspend == actorsSuspended_) {
		return;
	}
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;

	if (suspend) {
		suspendedBrains_.clear();
		if (scene) {
			for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
				if (!object) {
					continue;
				}
				// **有効なものだけ覚えて切る。** どちらが操作キャラかはPartyManagerが決めているので、
				// こちらで決め直さず「元に戻す」ことだけを受け持つ。
				if (Player* input = object->GetComponent<Player>()) {
					suspendedBrains_.push_back({input, input->IsEnabled()});
					input->SetEnabled(false);
				}
				if (AllyAIBrain* ai = object->GetComponent<AllyAIBrain>()) {
					suspendedBrains_.push_back({ai, ai->IsEnabled()});
					ai->SetEnabled(false);
				}
			}
		}
		if (GameObject* boss = FindBoss()) {
			if (GuardianBossComponent* brain = boss->GetComponent<GuardianBossComponent>()) {
				brain->SetSuspended(true);
			}
		}
	} else {
		for (const SuspendedBrain& entry : suspendedBrains_) {
			if (entry.component) {
				entry.component->SetEnabled(entry.wasEnabled);
			}
		}
		suspendedBrains_.clear();
		if (GameObject* boss = FindBoss()) {
			if (GuardianBossComponent* brain = boss->GetComponent<GuardianBossComponent>()) {
				brain->SetSuspended(false);
			}
		}
	}
	actorsSuspended_ = suspend;
}

void BossIntroCutscene::ApplyShot(const Vector3& position, const Vector3& lookAt) {
	if (OrbitCameraComponent* camera = FindCamera()) {
		camera->SetCutsceneShot(position, lookAt);
	}
}

void BossIntroCutscene::UpdateRevealShot(float progress) {
	const float eased = SmoothStep(progress);
	const Vector3 boss = BossPosition();
	const Vector3 front = FrontDirection();

	// 二人の居る側を基準に、回り込みながら引いていく。
	const float baseAngle = std::atan2(front.x, front.z);
	const float angle = baseAngle + revealOrbitDeg_ * std::numbers::pi_v<float> / 180.0f * eased;
	const float distance = revealStartDistance_ + (revealEndDistance_ - revealStartDistance_) * eased;
	const float height = revealStartHeight_ + (revealEndHeight_ - revealStartHeight_) * eased;

	const Vector3 position = {boss.x + std::sin(angle) * distance, boss.y + height, boss.z + std::cos(angle) * distance};
	// **見る高さも一緒に上げる。** 足元だけを見続けると、引いても大きさが伝わらない。
	const Vector3 lookAt = {boss.x, boss.y + bossLookHeight_ * (0.35f + 0.65f * eased), boss.z};

	revealEndPosition_ = position;
	revealEndLookAt_ = lookAt;
	ApplyShot(position, lookAt);
}

void BossIntroCutscene::UpdateApproachShot(float progress) {
	const float eased = SmoothStep(progress);
	const Vector3 leader = LeaderPosition();
	const Vector3 front = FrontDirection();

	// 着地点は通常視点と同じ「二人の背後からボスを見る」画。
	// **ここを合わせておかないと、カメラを返した瞬間に画がワープする。**
	const Vector3 landingPosition = {
	    leader.x + front.x * followDistance_, leader.y + followHeight_, leader.z + front.z * followDistance_};
	const Vector3 landingLookAt = {leader.x, leader.y + pivotHeight_, leader.z};

	const Vector3 position = revealEndPosition_ + (landingPosition - revealEndPosition_) * eased;
	const Vector3 lookAt = revealEndLookAt_ + (landingLookAt - revealEndLookAt_) * eased;
	ApplyShot(position, lookAt);
}

void BossIntroCutscene::Finish() {
	if (OrbitCameraComponent* camera = FindCamera()) {
		camera->EndCutscene();
	}
	SuspendActors(false);
	SetObjectActive(nameObjectName_, false);
	SetObjectActive(bossHudName_, true);
	phase_ = Phase::Done;
}

void BossIntroCutscene::Update() {
	if (phase_ == Phase::Done) {
		return;
	}

	// **必ず実時間。** ポーズもヒットストップも無関係に進めたい。
	const float deltaTime = Time::GetUnscaledDeltaTime();

	// 最初のUpdateでカメラと操作を預かる。**OnPlayStartではやらない**のは、
	// PartyManagerがどちらを操作キャラにするか決め終わるのを待つため。
	if (!actorsSuspended_) {
		SuspendActors(true);
		if (OrbitCameraComponent* camera = FindCamera()) {
			camera->BeginCutscene(blendSpeed_);
		}
		// 1フレーム目から寄せ始められるよう、始点の画をここで作る。
		UpdateRevealShot(0.0f);
	}

	if (allowSkip_ && IsSkipTriggered()) {
		Finish();
		return;
	}

	timer_ += deltaTime;

	switch (phase_) {
	case Phase::Reveal: {
		const float span = (std::max)(revealSeconds_, 0.01f);
		UpdateRevealShot(timer_ / span);
		if (timer_ >= revealSeconds_) {
			phase_ = Phase::Name;
			timer_ = 0.0f;
			SetObjectActive(nameObjectName_, true);
		}
		break;
	}

	case Phase::Name: {
		// 引ききった画のまま止めて、名前だけを浮かび上がらせる。
		ApplyShot(revealEndPosition_, revealEndLookAt_);
		{
			const float fade = (std::max)(nameFadeSeconds_, 0.01f);
			// 出るとき/消えるときの両側をなめらかにする。
			float alpha = std::clamp(timer_ / fade, 0.0f, 1.0f);
			alpha = (std::min)(alpha, std::clamp((nameSeconds_ - timer_) / fade, 0.0f, 1.0f));
			ApplyNameAlpha(alpha);
		}
		if (timer_ >= nameSeconds_) {
			phase_ = Phase::Approach;
			timer_ = 0.0f;
			SetObjectActive(nameObjectName_, false);
		}
		break;
	}

	case Phase::Approach: {
		const float span = (std::max)(approachSeconds_, 0.01f);
		UpdateApproachShot(timer_ / span);
		if (timer_ >= approachSeconds_) {
			Finish();
		}
		break;
	}

	default:
		break;
	}
}
