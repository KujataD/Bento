#include "GuardianBody.h"

#include "GuardianGait.h"
#include "IGuardianLegRig.h"
#include "GuardianRigMath.h"

#include <math/Noise.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using KujataEngine::GameObject;
using KujataEngine::Vector3;
using KujataEngine::WorldTransform;

namespace {

constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

/// <summary>rate[1/s]の指数追従。rateが0以下なら即座に目標値へ飛ばす。</summary>
float Approach(float current, float target, float rate, float deltaTime) {
	if (rate <= 0.0f) {
		return target;
	}
	float t = std::clamp(rate * deltaTime, 0.0f, 1.0f);
	return current + (target - current) * t;
}

/// <summary>
/// ownerの直下の子からname一致のGameObjectを探す(孫は見ない)。
/// 見つからなくてもエラーにしない = 未対応のプレハブ(BodyMeshがBodyの子のままの構成)は
/// 従来どおりBodyの回転をそのまま継承する。
/// </summary>
GameObject* FindDirectChildByName(GameObject* parent, const char* name) {
	if (!parent) {
		return nullptr;
	}
	for (GameObject* child : parent->GetChildren()) {
		if (child && child->GetName() == name) {
			return child;
		}
	}
	return nullptr;
}

} // namespace

void GuardianBody::OnPlayStart() {
	initialized_ = false;
	bobPhase_ = 0.0f;
	noiseTime_ = 0.0f;
	currentSpin_ = 0.0f;
	// **非シリアライズの状態はPlayごとに必ず戻す。** コンポーネントは使い回されるので、
	// 前回Playで分離したまま終わると2回目が分離状態から始まる。
	ResetEye();
	heightOffset_ = 0.0f;
	pitchOffset_ = 0.0f;
}

void GuardianBody::Update() {
	IGuardianLegRig* rig = GetComponent<IGuardianLegRig>();
	GameObject* owner = GetOwner();
	if (!rig || !owner) {
		return;
	}

	GameObject* bodyObject = rig->GetBodyObject();
	// ボディが解決できていない、またはルート自身が返っている場合は動かさない
	// (ルートを動かすと歩行の基準位置ごとずれてしまうため)。
	if (!bodyObject || bodyObject == owner) {
		return;
 	}

	float deltaTime = Time::GetDeltaTime();
	if (deltaTime <= 0.0f) {
		return;
	}

	const GuardianGait* gait = GetComponent<GuardianGait>();

	// --- 接地している足から、あるべき高さと傾きを求める ---
	float averageHeight = 0.0f;
	float pitchSlope = 0.0f;
	float rollSlope = 0.0f;
	bool hasFeet = GatherPlantedFeet(*rig, gait, averageHeight, pitchSlope, rollSlope);

	GuardianRigMath::WorldPose rootPose = GuardianRigMath::ComputeWorldPose(owner);

	// 全脚が浮いていれば足を参照しようがないので、ルートからの定位置へ戻る(ジャンプ中の姿勢)。
	float targetHeight = rideHeight_;
	if (hasFeet) {
		// ボディはルートの直下にぶら下がっている前提で、ワールド高さをローカルYへ落とす。
		float followHeight = (averageHeight + rideHeight_) - rootPose.position.y;

		// ここでクランプしないと、ルートのYがそのまま打ち消されて胴体のワールド高さが
		// 「足の高さ + rideHeight」に固定される。するとルートを上げても接合部が上がらず、
		// 脚が伸びきらないので GuardianGait の離脱判定が永久に発火しない(ジャンプにならない)。
		// 足への追従はサスペンションのストローク内に限り、それを超える分はルートに引かれる。
		targetHeight = std::clamp(followHeight, rideHeight_ - maxSuspensionTravel_, rideHeight_ + maxSuspensionTravel_);
	}

	float targetPitch = 0.0f;
	float targetRoll = 0.0f;
	if (hasFeet && alignToFeet_) {
		float maxTilt = maxTiltDeg_ * kDegreeToRadian;
		targetPitch = std::clamp(std::atan(pitchSlope) * tiltStrength_, -maxTilt, maxTilt);
		targetRoll = std::clamp(std::atan(rollSlope) * tiltStrength_, -maxTilt, maxTilt);
	}

	if (!initialized_) {
		currentHeight_ = targetHeight;
		currentPitch_ = targetPitch;
		currentRoll_ = targetRoll;
		initialized_ = true;
	} else {
		currentHeight_ = Approach(currentHeight_, targetHeight, heightSmoothing_, deltaTime);
		currentPitch_ = Approach(currentPitch_, targetPitch, tiltSmoothing_, deltaTime);
		currentRoll_ = Approach(currentRoll_, targetRoll, tiltSmoothing_, deltaTime);
	}

	// --- 歩行に合わせた上下の揺れ。速く歩くほど大きく揺れる ---
	float speed = gait ? gait->GetPlanarSpeed() : 0.0f;
	float speedFactor = std::clamp(speed / (std::max)(bobSpeedReference_, 1.0e-3f), 0.0f, 1.0f);
	bobPhase_ += deltaTime * bobFrequency_ * std::numbers::pi_v<float> * 2.0f * speedFactor;
	if (bobPhase_ > std::numbers::pi_v<float> * 2.0f) {
		bobPhase_ -= std::numbers::pi_v<float> * 2.0f;
	}
	float bob = std::sin(bobPhase_) * bobAmplitude_ * speedFactor;

	// --- 立ち止まっている間の揺れ。歩行のBobとちょうど入れ替わりで効く ---
	// **正弦波ではなくノイズにするのが要点。** 周期が読めないので機械的にならず、
	// 巨体が自重で微かに軋んでいるように見える。
	noiseTime_ += deltaTime;
	float idleSway = 0.0f;
	if (idleSwayAmplitude_ > 0.0f) {
		// 脚側(GuardianGait)とはレーンも周波数も別にしてある。噛み合うと共振して不自然に揺れる。
		idleSway = KujataEngine::PerlinNoise(noiseTime_ * idleSwayFrequency_ + 1.3f, 41.7f) * idleSwayAmplitude_ *
		           (1.0f - speedFactor);
	}

	// --- ボディだけを回す。脚は接地したままなので球体が独立して回って見える ---
	currentSpin_ += spinSpeedDeg_ * kDegreeToRadian * deltaTime;
	if (currentSpin_ > std::numbers::pi_v<float> * 2.0f) {
		currentSpin_ -= std::numbers::pi_v<float> * 2.0f;
	} else if (currentSpin_ < -std::numbers::pi_v<float> * 2.0f) {
		currentSpin_ += std::numbers::pi_v<float> * 2.0f;
	}

	WorldTransform& transform = bodyObject->GetTransform();
	// heightOffset_ は平滑化の外側で足す。溜めの沈み込みなど、即座に効いてほしい用途のため。
	// reactionSink_ / reactionPitch_ / reactionRoll_ は**被弾リアクションの加算レイヤー**。
	// 攻撃が書いた姿勢を消さずに上へ混ぜるので、技を出したまま殴られた反応が返る。
	transform.translation_.y = currentHeight_ + bob + idleSway + heightOffset_ + reactionSink_;
	// pitchOffset_ は平滑化の外側で足す。仰け反りは即座に効いてほしいため。
	transform.rotation_ = {currentPitch_ + pitchOffset_ + reactionPitch_, currentSpin_, currentRoll_ + reactionRoll_};

	// --- 目(見た目の本体)。**土台とは完全に分離して置く。** ---
	//
	// 土台(Body)は接合部をぶら下げるためだけの器で、足の接地から来る高さと傾きを受ける。
	// 目はそのどちらも受け継がない — 足の傾きを受けないのは以前からだが、
	// 分離中(eyeDetached_)は高さも受け継がず、攻撃が指定したローカル高さへそのまま置く。
	// これで「目だけ地面に降ろして脚を宙に浮かせる」が、歩行にも接合部にも触らずに書ける。
	if (GameObject* eyeObject = GetEyeObject()) {
		WorldTransform& eyeTransform = eyeObject->GetTransform();
		if (eyeObject->GetParent() != owner) {
			// **親子でない目**(第2形態)。ローカル=ワールドなので、指示された位置をそのまま置く。
			// 土台の車高もルートのYawも一切継承しない — これが「完全に分離」の実体。
			eyeTransform.translation_ = eyeOffset_ + reactionOffset_;
			// **正面合わせ。** ルートの向きを継がないぶん、第1形態と180度ずれていたのをここで揃える。
			eyeTransform.rotation_ = {eyePitch_ + reactionPitch_,
			    eyeYaw_ + detachedEyeYawOffsetDeg_ * (std::numbers::pi_v<float> / 180.0f), reactionRoll_};
		} else {
			float baseHeight = eyeDetached_ ? 0.0f : transform.translation_.y;
			eyeTransform.translation_ = {eyeOffset_.x + reactionOffset_.x, baseHeight + eyeOffset_.y + reactionOffset_.y,
			    eyeOffset_.z + reactionOffset_.z};
			// 親子の目にも同じ補正を掛ける。**両方の形態で同じだけ回す**ので正面が揃う。
			eyeTransform.rotation_ = {pitchOffset_ + eyePitch_ + reactionPitch_,
			    currentSpin_ + eyeYaw_ + detachedEyeYawOffsetDeg_ * (std::numbers::pi_v<float> / 180.0f), reactionRoll_};
		}
	}
}

GameObject* GuardianBody::GetEyeObject() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return nullptr;
	}
	if (GameObject* eye = FindDirectChildByName(owner, eyeObjectName_.c_str())) {
		return eye;
	}
	// 旧プレハブ互換。名前を変える前のデータでも動かす。
	if (GameObject* legacy = FindDirectChildByName(owner, "BodyMesh")) {
		return legacy;
	}
	// **親子でない目もここで拾う。**
	// 第2形態の目は脚から完全に切り離した独立オブジェクトなので、子には居ない。
	// シーンから名前で引く(1体ぶんなので探索の回数は問題にならない)。
	if (owner->GetScene() && !eyeObjectName_.empty()) {
		return owner->GetScene()->FindGameObjectByName(eyeObjectName_);
	}
	return nullptr;
}

bool GuardianBody::IsEyeDetachedObject() const {
	GameObject* eye = GetEyeObject();
	return eye && eye->GetParent() != GetOwner();
}

bool GuardianBody::GatherPlantedFeet(
    IGuardianLegRig& rig, const GuardianGait* gait, float& outAverageHeight, float& outPitchSlope, float& outRollSlope) const {

	// 0=前左, 1=前右, 2=後右, 3=後左。
	float heights[kGuardianLegCount] = {0.0f, 0.0f, 0.0f, 0.0f};
	bool planted[kGuardianLegCount] = {false, false, false, false};

	float total = 0.0f;
	int count = 0;

	const int legCount = rig.GetLegCount();
	for (int index = 0; index < legCount; ++index) {
		// 踏み出し中や攻撃で持ち上げ中の脚は、ボディを持ち上げる根拠にならないので除外する。
		if (rig.IsCurveDriven(index)) {
			continue;
		}
		if (gait && !gait->IsPlanted(index)) {
			continue;
		}

		heights[index] = rig.GetProceduralTarget(index).y;
		planted[index] = true;
		total += heights[index];
		++count;
	}

	if (count == 0) {
		return false;
	}
	outAverageHeight = total / static_cast<float>(count);

	// --- 傾きは「足の定位置」で重み付けして求める ---
	// **固定の添字で前後左右を組まない。** 0=前左/1=前右/2=後右/3=後左 という並びを前提にすると
	// 脚を3本に減らした瞬間に前後の対応が壊れる。定位置のx(左右)とz(前後)を重みに使えば、
	// 何本でも・どんな配置でも同じ式で扱える。4脚の既定配置では従来と同じ値になる。
	float pitchNumerator = 0.0f;
	float pitchWeight = 0.0f;
	float rollNumerator = 0.0f;
	float rollWeight = 0.0f;

	for (int index = 0; index < legCount; ++index) {
		if (!planted[index]) {
			continue;
		}
		Vector3 home = rig.GetHomeLocal(index);
		// 前が高いほど頭を上げたいので、zの符号を反転して掛ける。
		pitchNumerator += heights[index] * -home.z;
		pitchWeight += std::fabs(home.z);
		rollNumerator += heights[index] * home.x;
		rollWeight += std::fabs(home.x);
	}

	outPitchSlope = 0.0f;
	outRollSlope = 0.0f;
	// 重みの半分で割ると、4脚の既定配置で従来の「後ろ平均 - 前平均」と一致する。
	if (pitchWeight > 1.0e-4f) {
		outPitchSlope = pitchNumerator / (pitchWeight * 0.5f);
	}
	if (rollWeight > 1.0e-4f) {
		outRollSlope = rollNumerator / (rollWeight * 0.5f);
	}

	return true;
}
