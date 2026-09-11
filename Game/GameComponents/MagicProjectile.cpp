#include "MagicProjectile.h"
#include "GameFx.h"
#include "PlayerHealth.h"
#include "EnemyHealth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

namespace {

Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
	float length = Length(v);
	if (length > 0.0001f) {
		return v / length;
	}
	return fallback;
}

} // namespace

void MagicProjectile::Fire(const Vector3& direction, float speed, float lifetime, float damage, float poiseDamage, bool pierce, float hitInterval) {
	direction_ = Normalized(direction, {0.0f, 0.0f, 1.0f});
	speed_ = speed;
	lifetime_ = lifetime;
	damage_ = damage;
	poiseDamage_ = poiseDamage;
	pierce_ = pierce;
	hitInterval_ = hitInterval;
	hitCooldowns_.clear();
	homingTarget_ = nullptr;
	satellite_ = false;
	satelliteParent_ = nullptr;
	spreadRadius_ = 0.0f;
	spreadSeconds_ = 0.0f;
	spreadAngle_ = 0.0f;
	spreadElapsed_ = 0.0f;
	spreadOffset_ = {0.0f, 0.0f, 0.0f};
}

void MagicProjectile::SetSpread(float radius, float seconds, float angleDeg) {
	spreadRadius_ = radius;
	spreadSeconds_ = seconds;
	spreadAngle_ = angleDeg * std::numbers::pi_v<float> / 180.0f;
	spreadElapsed_ = 0.0f;
	spreadOffset_ = {0.0f, 0.0f, 0.0f};
}

Vector3 MagicProjectile::ConsumeSpreadDelta(float deltaTime) {
	if (spreadRadius_ <= 0.0f || spreadSeconds_ <= 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}

	spreadElapsed_ += deltaTime;
	float t = std::clamp(spreadElapsed_ / spreadSeconds_, 0.0f, 1.0f);

	// **片道で外へ開く**(戻さない)。戻してしまうと「散って集まる」だけの動きになり、
	// 散開した位置からホーミングで敵へ収束する、という意図した軌道にならない。
	// 立ち上がりを速くして、発射直後に一気に開くようにする(EaseOut)。
	float opened = 1.0f - (1.0f - t) * (1.0f - t);
	float radius = spreadRadius_ * opened;

	// 進行方向を軸にした円周上の向き。弾ごとにspreadAngle_をずらすと輪になる。
	Vector3 up = {0.0f, 1.0f, 0.0f};
	if (std::fabs(Dot(direction_, up)) > 0.95f) {
		up = {1.0f, 0.0f, 0.0f};
	}
	Vector3 right = Normalized(Cross(direction_, up), {1.0f, 0.0f, 0.0f});
	Vector3 planeUp = Normalized(Cross(right, direction_), {0.0f, 1.0f, 0.0f});

	Vector3 offset = right * (std::cos(spreadAngle_) * radius) + planeUp * (std::sin(spreadAngle_) * radius);

	// 直進成分と足し合わせるため、位置そのものではなく前フレームからの差分を返す。
	Vector3 delta = offset - spreadOffset_;
	spreadOffset_ = offset;

	if (t >= 1.0f) {
		// 開き切ったら散開は終わり。**横のズレは position に残したまま**、
		// 以降はホーミング(または直進)がその位置から敵へ導く。
		spreadRadius_ = 0.0f;
	}
	return delta;
}

void MagicProjectile::FireAsSatellite(GameObject* parent, float radius, float orbitSpeedDeg, float phaseDeg, float damage, float poiseDamage, float hitInterval) {
	satellite_ = true;
	satelliteParent_ = parent;
	satelliteRadius_ = radius;
	satelliteOrbitSpeedDeg_ = orbitSpeedDeg;
	satelliteAngleDeg_ = phaseDeg;
	damage_ = damage;
	poiseDamage_ = poiseDamage;
	pierce_ = true;
	hitInterval_ = hitInterval;
	hitCooldowns_.clear();
	homingTarget_ = nullptr;
	// 寿命は親に従う(自分では数えない)。IsAlive用に正の値を入れておく。
	lifetime_ = 1.0f;
	speed_ = 0.0f;
	UpdateSatellite(0.0f);
}

void MagicProjectile::SetHomingTarget(GameObject* target, float aimHeight, float turnRateDeg) {
	homingTarget_ = target;
	homingAimHeight_ = aimHeight;
	homingTurnRateDeg_ = turnRateDeg;
}

void MagicProjectile::Update() {
	if (!owner_ || lifetime_ <= 0.0f) {
		return;
	}

	float deltaTime = Time::GetDeltaTime();

	// 貫通の再ヒットクールダウンを進める。
	for (auto it = hitCooldowns_.begin(); it != hitCooldowns_.end();) {
		it->second -= deltaTime;
		if (it->second <= 0.0f) {
			it = hitCooldowns_.erase(it);
		} else {
			++it;
		}
	}

	if (satellite_) {
		if (!UpdateSatellite(deltaTime)) {
			Expire();
		}
		return;
	}

	if (homingTarget_ && homingTurnRateDeg_ > 0.0f) {
		SteerTowardsTarget(deltaTime);
	}

	// 直進成分 + 散開の横ずれ。散開が終われば後者は0になり、以降はまっすぐ(またはホーミング)。
	owner_->GetTransform().translation_ += direction_ * (speed_ * deltaTime) + ConsumeSpreadDelta(deltaTime);

	lifetime_ -= deltaTime;
	if (lifetime_ <= 0.0f) {
		Expire();
	}
}

void MagicProjectile::SteerTowardsTarget(float deltaTime) {
	if (!homingTarget_->IsActiveInHierarchy()) {
		homingTarget_ = nullptr;
		return;
	}
	Vector3 aim = homingTarget_->GetTransform().translation_ + Vector3{0.0f, homingAimHeight_, 0.0f};
	Vector3 desired = Normalized(aim - owner_->GetTransform().translation_, direction_);

	float cosAngle = std::clamp(Dot(direction_, desired), -1.0f, 1.0f);
	float angle = std::acos(cosAngle);
	float maxStep = homingTurnRateDeg_ * std::numbers::pi_v<float> / 180.0f * deltaTime;
	if (angle <= maxStep || angle < 1.0e-4f) {
		direction_ = desired;
		return;
	}
	// 球面線形補間で maxStep だけ回す。
	float sinAngle = std::sin(angle);
	Vector3 rotated = direction_ * (std::sin(angle - maxStep) / sinAngle) + desired * (std::sin(maxStep) / sinAngle);
	direction_ = Normalized(rotated, desired);
}

bool MagicProjectile::UpdateSatellite(float deltaTime) {
	if (!satelliteParent_ || !satelliteParent_->IsActiveInHierarchy() || !owner_) {
		return false;
	}
	satelliteAngleDeg_ += satelliteOrbitSpeedDeg_ * deltaTime;

	// 親弾の進行方向に直交する面で公転する。
	Vector3 axis = {0.0f, 0.0f, 1.0f};
	if (MagicProjectile* parentProjectile = satelliteParent_->GetComponent<MagicProjectile>()) {
		axis = parentProjectile->GetDirection();
	}
	Vector3 up = {0.0f, 1.0f, 0.0f};
	if (std::fabs(Dot(axis, up)) > 0.95f) {
		up = {1.0f, 0.0f, 0.0f};
	}
	Vector3 right = Normalized(Cross(axis, up), {1.0f, 0.0f, 0.0f});
	Vector3 planeUp = Normalized(Cross(right, axis), {0.0f, 1.0f, 0.0f});

	float angle = satelliteAngleDeg_ * std::numbers::pi_v<float> / 180.0f;
	Vector3 offset = right * (std::cos(angle) * satelliteRadius_) + planeUp * (std::sin(angle) * satelliteRadius_);
	owner_->GetTransform().translation_ = satelliteParent_->GetTransform().translation_ + offset;
	return true;
}

void MagicProjectile::Expire() {
	lifetime_ = 0.0f;
	satellite_ = false;
	satelliteParent_ = nullptr;
	homingTarget_ = nullptr;
	hitCooldowns_.clear();
	if (owner_) {
		owner_->SetActive(false);
	}
}

void MagicProjectile::SpawnHitEffect() {
	// 当たった場所で弾ける。加算合成の小さな火花なので、当たり所が一目で分かる。
	if (owner_) {
		GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kMagicHit, owner_->GetTransform().GetWorldPosition());
	}
}

void MagicProjectile::OnTriggerStay(KujataEngine::ColliderComponent* other) {
	if (!other || lifetime_ <= 0.0f) {
		return;
	}

	KujataEngine::GameObject* otherObj = other->GetOwner();
	if (!otherObj) {
		return;
	}

	// --- 当たらないもの ---
	// **味方キャラは素通り。** 撃った本人にも相方にも当たらない(誤射で事故らせない)。
	// 倒れている相方も同じく素通りする。蘇生は撃って進めるものではなく、
	// 倒れてからの経過時間で自力に起き上がる([[death-and-revive]])。
	if (otherObj->GetComponentInParent<PlayerHealth>()) {
		return;
	}
	// 弾同士でぶつかると、斉射したそばから自分たちで潰し合ってしまう。
	if (otherObj->GetComponentInParent<MagicProjectile>()) {
		return;
	}

	// --- ここから先はすべて当たる ---
	// 敵でも地形でも壁でも、当たったらそこで弾ける。**すり抜けないことが要点**で、
	// 障害物の裏へ逃げれば魔法をやり過ごせる、という遊びが成立する。
	SpawnHitEffect();

	EnemyHealth* health = otherObj->GetComponentInParent<EnemyHealth>();
	if (!health) {
		// 地形など、ダメージの受け手がいないものに当たった。消えるだけ。
		if (!pierce_) {
			Expire();
		}
		return;
	}

	if (pierce_) {
		// 貫通: 実体単位で再ヒット間隔を守りながら何度でも当たる。
		GameObject* target = health->GetOwner();
		if (!target || hitCooldowns_.count(target) > 0) {
			return;
		}
		health->TakeDamage(damage_, poiseDamage_, attacker_);
		hitCooldowns_[target] = hitInterval_;
		return;
	}

	health->TakeDamage(damage_, poiseDamage_, attacker_);
	Expire();
}
