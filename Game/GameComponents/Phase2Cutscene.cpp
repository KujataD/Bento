#include "Phase2Cutscene.h"

#include "GameAudio.h"
#include "GameFx.h"
#include "GameSession.h"
#include "GuardianBossComponent.h"
#include "ScreenFader.h"

#include <Editor/PrefabAsset.h>
#include <components/OrbitCameraComponent.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

namespace {

constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

/// <summary>-1〜1の擬似乱数。**時刻から作る**ので、同じ拍では同じ揺れ方になる。</summary>
float Wobble(float seed) {
	float value = std::sin(seed * 127.1f) * 43758.5453f;
	return (value - std::floor(value)) * 2.0f - 1.0f;
}

} // namespace

void Phase2Cutscene::OnPlayStart() {
	// **非シリアライズの状態はPlayごとに必ず戻す。** 演出の途中でPlayを止めると、
	// 次のPlayがカメラを預かったまま始まって操作できなくなる。
	phase_ = Phase::Idle;
	timer_ = 0.0f;
	shotsFired_ = 0;
	shakeTimer_ = 0.0f;
	dustTimer_ = 0.0f;
	beam_ = nullptr;
	beamTried_ = false;
	boss_ = nullptr;
}

Phase2Cutscene* Phase2Cutscene::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (Phase2Cutscene* cutscene = object->GetComponent<Phase2Cutscene>()) {
			return cutscene;
		}
	}
	return nullptr;
}

void Phase2Cutscene::Begin() {
	if (phase_ != Phase::Idle) {
		return;
	}
	phase_ = Phase::FadeToBlack;
	timer_ = 0.0f;
	shotsFired_ = 0;
	// **「まだ終わっていない」を音で突きつける。** 削り切った直後に鳴らすのが効く。
	GameAudio::PlaySe(GameAudio::Se::Phase2Transition);

	// **ボスの体をこちらで預かる。** 預からないとBTが裏で回り続け、
	// 演出で置いた位置から歩き出したり、跳んでいる最中に攻撃を出したりする。
	if (GameObject* boss = FindBoss()) {
		if (GuardianBossComponent* brain = boss->GetComponent<GuardianBossComponent>()) {
			brain->SetSuspended(true);
		}
	}

	// 戦闘の曲はここで切る。以降は演出の音だけにする。
	GameAudio::StopBgm();

	if (ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
		fader->FadeOut(fadeInSeconds_);
	}
}

void Phase2Cutscene::Update() {
	if (phase_ == Phase::Idle) {
		return;
	}

	// 演出は実時間で進める(ヒットストップやポーズの影響を受けない)。
	const float deltaTime = Time::GetUnscaledDeltaTime();
	timer_ += deltaTime;
	if (shakeTimer_ > 0.0f) {
		shakeTimer_ -= deltaTime;
	}

	// 揺れは着弾からの経過で減衰させる。**残り時間の二乗**にすると、
	// 最初にどんと来て素早く収まる、という手触りになる。
	float shake = 0.0f;
	if (shakeSeconds_ > 0.0f && shakeTimer_ > 0.0f) {
		float t = shakeTimer_ / shakeSeconds_;
		shake = shakeStrength_ * t * t;
	}
	UpdateShot(shake);

	switch (phase_) {
	case Phase::FadeToBlack: {
		ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr);
		bool covered = !fader || fader->IsOpaque() || timer_ >= fadeInSeconds_ + 0.5f;
		if (!covered) {
			break;
		}
		if (timer_ < fadeInSeconds_ + holdBlackSeconds_) {
			break;
		}
		// **暗転の裏で跳ばせる。** 明けた瞬間に位置が変わっているほうが「移った」と伝わる。
		if (GameObject* boss = FindBoss()) {
			leapStart_ = boss->GetTransform().translation_;
		}
		phase_ = Phase::Leap;
		timer_ = 0.0f;
		break;
	}

	case Phase::Leap: {
		GameObject* boss = FindBoss();
		float t = std::clamp(timer_ / (std::max)(leapSeconds_, 0.01f), 0.0f, 1.0f);
		if (boss) {
			// 水平は等速、垂直は正弦の山。飛びかかりと同じ描き方で揃えている。
			Vector3 landing = arenaCenter_;
			landing.y = leapStart_.y;
			Vector3 position = {
			    std::lerp(leapStart_.x, landing.x, t),
			    std::lerp(leapStart_.y, landing.y, t) + std::sin(std::numbers::pi_v<float> * t) * leapHeight_,
			    std::lerp(leapStart_.z, landing.z, t),
			};
			boss->GetTransform().translation_ = position;
		}

		// 跳び始めと同時に明ける。空中にいる姿から見せることになる。
		if (timer_ >= leapSeconds_ * 0.15f) {
			if (ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
				if (fader->IsOpaque()) {
					fader->FadeIn(0.5f);
				}
			}
		}

		if (t >= 1.0f) {
			if (boss) {
				// 着地の土埃。ここから撃ち上げが始まる。
				GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, boss->GetTransform().translation_, 2.5f);
			}
			shakeTimer_ = shakeSeconds_;
			phase_ = Phase::Volley;
			timer_ = 0.0f;
		}
		break;
	}

	case Phase::Volley: {
		GameObject* boss = FindBoss();
		Vector3 origin = boss ? boss->GetTransform().translation_ : arenaCenter_;
		origin.y += 3.0f;

		// **1発ごとに狙いを少しずらす。ただしずらすのは向きだけで、発射口は動かさない。**
		// 根元まで動かすと、ビームが目から生えていないように見えてしまう。
		float jitterSeed = static_cast<float>(shotsFired_) + 1.0f;
		float tiltYaw = Wobble(jitterSeed) * std::numbers::pi_v<float>;
		float tiltPitch = (0.5f + 0.5f * Wobble(jitterSeed + 17.0f)) * shotJitter_ * kDegreeToRadian;

		// 撃っている間だけ柱を立てる。間隔の前半で出して後半は消す = 連射に見える。
		float sinceShot = timer_ - static_cast<float>(shotsFired_) * shotInterval_;
		if (sinceShot >= 0.0f && sinceShot < shotInterval_ * 0.55f) {
			ShowBeam(origin, 60.0f, 1.1f, tiltYaw, tiltPitch);
		} else {
			ShowBeam(origin, 0.0f, 0.0f, tiltYaw, tiltPitch);
		}

		int wanted = static_cast<int>(timer_ / (std::max)(shotInterval_, 0.01f)) + 1;
		wanted = (std::min)(wanted, shotCount_);
		while (shotsFired_ < wanted) {
			++shotsFired_;
			// 撃った瞬間に画面が揺れる。**当たった先(天井)は映さない**ので、
			// 「何かに当たった」ことは揺れと、降ってくる土埃だけで伝える。
			shakeTimer_ = shakeSeconds_;
			GameAudio::PlaySe(GameAudio::Se::MagicShot);
			if (owner_) {
				// **1発当たるたびに上から降ってくる。** 撃ち終わってからまとめて落とすと
				// 「撃った→崩れた」の因果が切れて、ただの演出の順番待ちに見える。
				// 天井の高さで散らしてから落とすので、視線を上げなくても降ってくるのが見える。
				for (int index = 0; index < 3; ++index) {
					float seed = static_cast<float>(shotsFired_) * 7.3f + static_cast<float>(index);
					Vector3 high = origin;
					high.x += Wobble(seed) * 9.0f;
					high.z += Wobble(seed + 5.0f) * 9.0f;
					high.y += 16.0f + Wobble(seed + 11.0f) * 5.0f;
					GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, high, dustStrength_ * 0.7f);
				}
			}
		}

		if (shotsFired_ >= shotCount_ && timer_ >= static_cast<float>(shotCount_) * shotInterval_) {
			ShowBeam(origin, 0.0f, 0.0f, tiltYaw, tiltPitch);
			phase_ = Phase::Dust;
			timer_ = 0.0f;
			dustTimer_ = 0.0f;
		}
		break;
	}

	case Phase::Dust: {
		GameObject* boss = FindBoss();
		Vector3 center = boss ? boss->GetTransform().translation_ : arenaCenter_;

		// **降ってくる量で見せる。** 1回の大きな塊より、間断なく落ちてくるほうが崩落に見える。
		dustTimer_ -= deltaTime;
		if (dustTimer_ <= 0.0f) {
			dustTimer_ = 0.09f;
			for (int index = 0; index < 3; ++index) {
				float seed = timer_ * 13.0f + static_cast<float>(index);
				Vector3 spot = center;
				spot.x += Wobble(seed) * 16.0f;
				spot.z += Wobble(seed + 7.0f) * 16.0f;
				spot.y += 6.0f + Wobble(seed + 3.0f) * 4.0f;
				GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, spot, dustStrength_);
			}
			shakeTimer_ = (std::max)(shakeTimer_, shakeSeconds_ * 0.4f);
		}

		if (timer_ >= dustSeconds_) {
			// 5. 土埃ごと暗転する。次に明けたときには空の下にいる。
			if (ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
				fader->FadeOut(dustFadeSeconds_);
			}
			phase_ = Phase::FadeOut;
			timer_ = 0.0f;
		}
		break;
	}

	case Phase::FadeOut: {
		ScreenFader* fader = ScreenFader::FindInScene(owner_ ? owner_->GetScene() : nullptr);
		bool covered = !fader || fader->IsOpaque() || timer_ >= dustFadeSeconds_ + 0.6f;
		if (!covered) {
			break;
		}
		// **カメラも体も返してから出る。** 預かったまま切り替えると次のシーンの初期化と噛み合わない。
		if (OrbitCameraComponent* camera = FindCamera()) {
			camera->EndCutscene();
		}
		if (GameObject* boss = FindBoss()) {
			if (GuardianBossComponent* brain = boss->GetComponent<GuardianBossComponent>()) {
				brain->SetSuspended(false);
			}
		}
		Time::SetTimeScale(1.0f);
		// **シーンの切り替えは必ず ScreenFader 経由で予約する。**
		// ここで直に ChangeScene すると、Update の最中に今のシーンごと自分が消える
		// (コンポーネントを回している最中に足元が無くなり、そのまま落ちる)。
		phase_ = Phase::Leaving;
		ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, nextSceneName_, true);
		break;
	}

	case Phase::Leaving:
	case Phase::Idle:
		break;
	}
}

void Phase2Cutscene::UpdateShot(float shake) {
	OrbitCameraComponent* camera = FindCamera();
	if (!camera) {
		return;
	}
	GameObject* boss = FindBoss();
	Vector3 focus = boss ? boss->GetTransform().translation_ : arenaCenter_;

	// **上からボスを見下ろす画。** こうすると天井が画面に入らないので、
	// 天井のモデルを用意せずに「撃ち抜いている」場面が成立する。
	Vector3 position = focus;
	position.y += cameraHeight_;
	position.z -= cameraBack_;

	if (shake > 0.0f) {
		float seed = Time::GetUnscaledDeltaTime() * 1000.0f + shakeTimer_ * 97.0f;
		position.x += Wobble(seed) * shake;
		position.y += Wobble(seed + 31.0f) * shake;
		position.z += Wobble(seed + 61.0f) * shake;
	}

	if (!camera->IsCutscene()) {
		// 初回だけカットシーンへ入る。BeginCutsceneは寄せの速さを決めるだけなので、
		// 毎フレーム呼ぶと寄り切らないまま速度だけ入れ直すことになる。
		camera->BeginCutscene(6.0f);
	}
	camera->SetCutsceneShot(position, focus);
}

GameObject* Phase2Cutscene::FindBoss() {
	if (boss_) {
		return boss_;
	}
	if (!owner_ || !owner_->GetScene() || bossName_.empty()) {
		return nullptr;
	}
	boss_ = owner_->GetScene()->FindGameObjectByName(bossName_);
	return boss_;
}

OrbitCameraComponent* Phase2Cutscene::FindCamera() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (OrbitCameraComponent* camera = object->GetComponent<OrbitCameraComponent>()) {
			return camera;
		}
	}
	return nullptr;
}

GameObject* Phase2Cutscene::AcquireBeam() {
	if (beam_) {
		return beam_;
	}
	if (beamTried_ || !owner_ || !owner_->GetScene() || beamPrefabPath_.empty()) {
		return nullptr;
	}
	beamTried_ = true;
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*owner_->GetScene(), beamPrefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[Phase2Cutscene] beam prefab load failed (" + beamPrefabPath_ + "): " + result.message);
		return nullptr;
	}
	beam_ = result.rootObject;
	beam_->SetActive(false);
	return beam_;
}

void Phase2Cutscene::ShowBeam(const Vector3& origin, float length, float thickness, float tiltYaw, float tiltPitch) {
	GameObject* beam = AcquireBeam();
	if (!beam) {
		return;
	}
	if (length <= 0.0f || thickness <= 0.0f) {
		if (beam->IsActive()) {
			beam->SetActive(false);
		}
		return;
	}

	// **発射口は動かさず、角度だけ振る。**
	// 根元を目に固定したまま向きだけ変えれば、同じ場所から扇状に掘っていく画になる。
	const float pitch = -std::numbers::pi_v<float> * 0.5f + tiltPitch;
	Vector3 forward = {std::sin(tiltYaw) * std::cos(pitch), -std::sin(pitch), std::cos(tiltYaw) * std::cos(pitch)};

	// Cubeは原点が中心なので、向いた方へ長さの半分だけ出して根元を合わせる。
	WorldTransform& transform = beam->GetTransform();
	transform.rotation_ = {pitch, tiltYaw, 0.0f};
	transform.scale_ = {thickness, thickness, length};
	transform.translation_ = origin + forward * (length * 0.5f);
	beam->SetActive(true);
}
