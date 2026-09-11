#include "MagicAbilitySet.h"
#include "GameEvents.h"
#include "Player.h"
#include "GameAudio.h"
#include "GameFx.h"
#include "CharacterMotor.h"
#include "IEnemy.h"
#include "LockOnController.h"
#include "MagicProjectile.h"
#include "StaminaComponent.h"

#include <Editor/PrefabAsset.h>
#include <scene/MovementUtil.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

using namespace KujataEngine;

namespace {

// 名前で子孫を探す(自分自身も対象)。
GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object) {
		return nullptr;
	}
	if (object->GetName() == name) {
		return object;
	}
	for (GameObject* child : object->GetChildren()) {
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

void MagicAbilitySet::OnPlayStart() {
	// 致命の溜め中にPlayを止めると、印が立ったままで杖の球が制御不能になる。必ず降ろす。
	criticalOrbActive_ = false;
	motor_ = GetComponent<CharacterMotor>();
	stamina_ = GetComponent<StaminaComponent>();
	animator_ = GetComponentInChildren<AnimatorComponent>();
	cooldownTimer_ = 0.0f;
	pending_ = PendingShot::None;
	castTimer_ = 0.0f;
	CancelVolley();
	// 杖の先の球はPlayインスタンスごとに解決し、まず消しておく。
	staffOrb_ = FindStaffOrb();
	if (staffOrb_) {
		staffOrb_->SetActive(false);
	}
	// 毎回同じ順番で散らないよう、Playごとに種を取り直す。
	random_.seed(std::random_device{}());
	// プールはPlayインスタンスごとに作り直す(前回Playの弾ポインタは無効)。
	projectilePool_.clear();
	baseScales_.clear();
}

void MagicAbilitySet::Update() {
	float deltaTime = Time::GetDeltaTime();
	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= deltaTime;
		if (cooldownTimer_ < 0.0f) {
			cooldownTimer_ = 0.0f;
		}
	}

	// 被弾・回避で中断されたら、詠唱も撃ち残しも取り消す(スタミナは戻さない)。
	if (motor_ && motor_->IsActionLocked()) {
		pending_ = PendingShot::None;
		CancelVolley();
		UpdateStaffOrb();
		return;
	}

	// 斉射は詠唱と独立して進む(1発目を撃った後は詠唱待ちではないため)。
	UpdateVolley(deltaTime);

	if (pending_ == PendingShot::None) {
		return;
	}

	castTimer_ += deltaTime;
	// 詠唱・溜めの進み具合に合わせて杖の球を育てる。
	UpdateStaffOrb();

	float delay = (pending_ == PendingShot::Charge) ? chargeFireDelay_ : fireDelay_;
	if (castTimer_ < delay) {
		return;
	}

	if (pending_ == PendingShot::Charge) {
		StartVolley();
	} else {
		FireNormal();
	}
	pending_ = PendingShot::None;
	cooldownTimer_ = cooldownSeconds_;
	// 撃った瞬間に球を消す(魔力が弾へ移った、という見え方)。
	UpdateStaffOrb();
}

bool MagicAbilitySet::TryUse(int slot) {
	if (!owner_ || (slot != 0 && slot != 1)) {
		return false;
	}
	if (cooldownTimer_ > 0.0f || pending_ != PendingShot::None) {
		return false;
	}
	// 斉射の途中に次を撃たせない。
	if (IsVolleyActive()) {
		return false;
	}
	if (motor_ && motor_->IsActionLocked()) {
		return false;
	}
	if (stamina_ && !stamina_->CanUse()) {
		// **操作中のキャラのときだけ掲示する。** AI相方も同じ技セットを使うので、
		// 門番が無いと相方の息切れで画面に文字が出る。
		if (Player::IsControlledObject(owner_)) {
			GameEvents::ReportFailure(GameEvents::Failure::NoStamina);
		}
		return false;
	}

	bool charge = (slot == 1);
	const std::string& clipName = charge ? chargeClipName_ : castClipName_;
	float delay = charge ? chargeFireDelay_ : fireDelay_;

	if (stamina_) {
		stamina_->ConsumePercent(charge ? staminaCostCharge_ : staminaCostNormal_);
	}

	// 詠唱モーション(クリップがあれば)。発射はFire Delay後にUpdateで行う。
	if (animator_ && !clipName.empty()) {
		animator_->PlayByName(clipName);
	}

	if (delay <= 0.0f) {
		if (charge) {
			StartVolley();
		} else {
			FireNormal();
		}
		cooldownTimer_ = cooldownSeconds_;
		return true;
	}

	pending_ = charge ? PendingShot::Charge : PendingShot::Normal;
	castTimer_ = 0.0f;
	return true;
}

bool MagicAbilitySet::IsBusy() const {
	if (pending_ != PendingShot::None || IsVolleyActive()) {
		return true;
	}
	// 詠唱クリップの再生中も忙しい(発射後の振り抜き)。クールダウンは含めない。
	if (animator_ && animator_->IsPlaying()) {
		const std::string& current = animator_->GetClip().name;
		if ((!castClipName_.empty() && current == castClipName_) || (!chargeClipName_.empty() && current == chargeClipName_)) {
			return true;
		}
	}
	return false;
}

bool MagicAbilitySet::FirePebble(GameObject* target) {
	if (!owner_) {
		return false;
	}
	GameObject* projectileObject = AcquireProjectile(projectilePool_, projectilePrefabPath_);
	if (!projectileObject) {
		return false;
	}

	Vector3 position;
	Vector3 forward;
	GetMuzzle(position, forward);
	if (target) {
		// 狙い点は敵側(IEnemy)が決める。持っていない相手には正面へ撃つ。
		Vector3 aim = target->GetTransform().translation_;
		if (IEnemy* enemy = target->GetComponentInParent<IEnemy>()) {
			aim = enemy->GetLockOnPoint();
		}
		Vector3 toTarget = aim - position;
		float length = Length(toTarget);
		if (length > 0.0001f) {
			forward = toTarget / length;
		}
	}

	WorldTransform& transform = projectileObject->GetTransform();
	transform.translation_ = position;
	// 小弾: 通常弾の見た目を縮めて使う(通常弾として再利用されるときはApplyScale(1)で戻る)。
	ApplyScale(projectileObject, 0.6f);
	projectileObject->SetActive(true);

	if (MagicProjectile* projectile = projectileObject->GetComponent<MagicProjectile>()) {
		projectile->SetAttacker(owner_);
		projectile->Fire(forward, projectileSpeed_ * 1.3f, projectileLifetime_, pebbleDamage_, pebblePoise_);
		if (target) {
			ApplyHoming(projectile, target);
		}
	}
	return true;
}

bool MagicAbilitySet::TryCritical(GameObject* target, float totalDamage) {
	if (!owner_) {
		return false;
	}
	(void)totalDamage;

	// --- 突き立てる地点を決める ---
	// 相手の足元。**相手の位置そのものではなく地面**なので、
	// 「持ち上げて刺す」ではなく「地面ごと貫く」という見え方になる。
	Vector3 stabPoint = owner_->GetTransform().translation_;
	if (target) {
		stabPoint = target->GetTransform().translation_;
	}
	stabPoint.y += stabHeightOffset_;

	// --- 杖を地面へ突き立てる ---
	// 溜めていた球は杖の先にあるので、それを刺し込む位置まで降ろしてから消す。
	// 魔力が球から地面へ移った、という流れを1つの動きで見せる。
	if (staffOrb_) {
		staffOrb_->UpdateWorldTransformSelfAndAncestors();
		Vector3 orbWorld = staffOrb_->GetTransform().GetWorldPosition();
		// 杖先から刺突点まで、魔力が流れ落ちる筋を撒く。
		GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kMagicHit, orbWorld, 0.8f);
	}

	// **刺した地点で魔力が炸裂する。** 弾をばら撒く方式と違い、決めた場所が1点に定まるので
	// 「そこへ突き立てた」ことが画面上ではっきり伝わる。
	Scene* scene = owner_->GetScene();
	GameFx::Burst(scene, GameFx::Prefab::kMagicHit, stabPoint, stabBurstStrength_);
	GameFx::Burst(scene, GameFx::Prefab::kDust, stabPoint, stabDustStrength_);

	// 溜めていた球は地面へ移したので消す。
	OnCriticalEnd();

	// **falseを返す。** ダメージはCriticalStrikeComponentの素の値に任せる。
	// 剣士と同じ威力にしたいので、こちらで独自に配分しない。
	return false;
}

void MagicAbilitySet::OnCriticalWindup(float progress) {
	if (!staffOrb_) {
		staffOrb_ = FindStaffOrb();
		if (!staffOrb_) {
			return;
		}
	}
	// 通常の詠唱とは別経路で球を出す(UpdateStaffOrbに消されないよう印を立てる)。
	criticalOrbActive_ = true;
	float size = criticalOrbSize_ * std::clamp(progress, 0.0f, 1.0f);
	staffOrb_->SetActive(true);
	staffOrb_->GetTransform().scale_ = {size, size, size};
}

void MagicAbilitySet::OnCriticalEnd() {
	criticalOrbActive_ = false;
	if (staffOrb_) {
		staffOrb_->SetActive(false);
	}
}

void MagicAbilitySet::GetMuzzle(Vector3& outPosition, Vector3& outForward) const {
	Vector3 forward = MovementUtil::GetForward(*owner_);
	forward.y = 0.0f;
	float length = Length(forward);
	if (length > 0.0001f) {
		forward = forward / length;
	} else {
		forward = {0.0f, 0.0f, 1.0f};
	}
	outPosition = owner_->GetTransform().translation_ + forward * muzzleForward_ + Vector3{0.0f, muzzleHeight_, 0.0f};
	outForward = forward;

	// --- 仰角 ---
	// **水平にしか撃てないと、浮いている相手に永久に当たらない。**
	// 体の向き(Yaw)は従来どおり水平のまま、弾の向きだけを狙いへ持ち上げる。
	// 狙いはZ注目の対象か、頭脳が指定した相手(AIはこちらを使う)。
	GameObject* aim = aimTarget_ ? aimTarget_ : FindLockOnTarget();
	if (!aim || maxElevationDeg_ <= 0.0f) {
		return;
	}

	// 狙い点は敵側(IEnemy)が決める。部位(目・脚)に当たっても親のIEnemyへ遡る。
	Vector3 aimPoint = aim->GetTransform().translation_;
	if (IEnemy* enemy = aim->GetComponentInParent<IEnemy>()) {
		aimPoint = enemy->GetLockOnPoint();
	}
	Vector3 toAim = aimPoint - outPosition;
	float horizontal = std::sqrt(toAim.x * toAim.x + toAim.z * toAim.z);
	if (horizontal <= 0.0001f) {
		return;
	}

	// 上下の角度だけを取り出し、行き過ぎないよう上限で丸める。
	float elevation = std::atan(toAim.y / horizontal);
	float limit = maxElevationDeg_ * (std::numbers::pi_v<float> / 180.0f);
	elevation = std::clamp(elevation, -limit, limit);

	// 水平成分は体の向きのまま。持ち上げるのは縦だけなので、横の狙いは従来と変わらない。
	float cosE = std::cos(elevation);
	outForward = {forward.x * cosE, std::sin(elevation), forward.z * cosE};
}

GameObject* MagicAbilitySet::FindStaffOrb() {
	if (!owner_ || orbObjectName_.empty()) {
		return nullptr;
	}
	return FindDescendantByName(owner_, orbObjectName_);
}

void MagicAbilitySet::UpdateStaffOrb() {
	// 致命の溜めが球を使っている間は、通常の詠唱側は手を出さない。
	if (criticalOrbActive_) {
		return;
	}
	if (!staffOrb_) {
		// シーンによっては球を置いていないこともあるので、見つからなければ何もしない。
		staffOrb_ = FindStaffOrb();
		if (!staffOrb_) {
			return;
		}
	}

	// 球が出るのは詠唱・溜めの最中だけ。撃ち始めたら(pending_が解けたら)消える。
	if (pending_ == PendingShot::None) {
		if (staffOrb_->IsActive()) {
			staffOrb_->SetActive(false);
		}
		return;
	}

	bool charge = (pending_ == PendingShot::Charge);
	float delay = charge ? chargeFireDelay_ : fireDelay_;
	float maxSize = charge ? orbSizeCharge_ : orbSizeNormal_;

	// 発射までの進み具合(0→1)にそのまま比例させて育てる。
	float progress = (delay > 0.0f) ? std::clamp(castTimer_ / delay, 0.0f, 1.0f) : 1.0f;
	float size = maxSize * progress;

	staffOrb_->SetActive(true);
	staffOrb_->GetTransform().scale_ = {size, size, size};
}

GameObject* MagicAbilitySet::FindLockOnTarget() const {
	if (!owner_) {
		return nullptr;
	}
	LockOnController* lockOn = LockOnController::FindInScene(owner_->GetScene());
	return lockOn ? lockOn->GetTarget() : nullptr;
}

void MagicAbilitySet::ApplyHoming(MagicProjectile* projectile, GameObject* target) {
	if (!projectile || !target || homingTurnRateDeg_ <= 0.0f) {
		return;
	}
	// 狙い点は敵側(IEnemy)が決める。弾へは「対象位置からの高さ」で渡す規約なので差分に直す。
	float height = 0.0f;
	if (IEnemy* enemy = target->GetComponentInParent<IEnemy>()) {
		height = enemy->GetLockOnPoint().y - target->GetTransform().translation_.y;
	}
	projectile->SetHomingTarget(target, height, homingTurnRateDeg_);
}

void MagicAbilitySet::FireBolt(float spreadAngleDeg, GameObject* lockOnTarget) {
	GameObject* projectileObject = AcquireProjectile(projectilePool_, projectilePrefabPath_);
	if (!projectileObject) {
		return;
	}

	// 発射のたびに銃口を取り直す。斉射は時間をまたぐので、その間に向きを変えれば弾もついてくる。
	Vector3 position;
	Vector3 forward;
	GetMuzzle(position, forward);

	// 位置だけ発射時に決める(スケール等の見た目はPrefab側の定義を尊重する)。
	WorldTransform& transform = projectileObject->GetTransform();
	transform.translation_ = position;
	ApplyScale(projectileObject, 1.0f);
	projectileObject->SetActive(true);

	MagicProjectile* projectile = projectileObject->GetComponent<MagicProjectile>();
	if (!projectile) {
		return;
	}
	projectile->SetAttacker(owner_);
	projectile->Fire(forward, projectileSpeed_, projectileLifetime_, projectileDamage_, projectilePoise_);
	projectile->SetSpread(spreadRadius_, spreadSeconds_, spreadAngleDeg);
	// Z注目中はホーミング(散開し切ったあと、その位置から対象へ吸い込まれる)。
	ApplyHoming(projectile, lockOnTarget);

	// **1発ごとに鳴らす。** 詠唱1回につき1回だと、斉射で8発出ても音は最初の1度きりで、
	// 「撃っている手応え」が弾の数と噛み合わない。斉射は時間をまたぐので音も連なる。
	GameAudio::PlaySe(GameAudio::Se::MagicShot);
}

void MagicAbilitySet::FireNormal() {
	GameObject* lockOnTarget = FindLockOnTarget();

	// 円周を等分した向きへ散らす(2発なら左右、4発なら十字)。
	int count = (std::max)(boltCount_, 1);
	for (int index = 0; index < count; ++index) {
		float angle = spreadPhaseOffset_ + 360.0f * static_cast<float>(index) / static_cast<float>(count);
		FireBolt(angle, lockOnTarget);
	}
}

void MagicAbilitySet::StartVolley() {
	int count = (std::max)(chargeBoltCount_, 1);

	// 円周を等分した散開角度を作り、**順番をシャッフルする**。
	// 等分した角度を順に撃つと弾が規則正しく回ってしまい、乱射している感じが出ない。
	volleyAngles_.resize(static_cast<size_t>(count));
	for (int index = 0; index < count; ++index) {
		volleyAngles_[static_cast<size_t>(index)] = spreadPhaseOffset_ + 360.0f * static_cast<float>(index) / static_cast<float>(count);
	}
	std::shuffle(volleyAngles_.begin(), volleyAngles_.end(), random_);

	// 狙いは撃ち始めの対象で固定する(1発ごとに探し直すと、途中で対象が変わったとき散らばるため)。
	volleyTarget_ = FindLockOnTarget();
	volleyIndex_ = 0;
	volleyTimer_ = 0.0f;

	// 1発目はこの場で撃つ。以降はUpdateVolleyが間隔ごとに撃つ。
	FireBolt(volleyAngles_[0], volleyTarget_);
	volleyIndex_ = 1;
	volleyTimer_ = chargeShotInterval_;
}

void MagicAbilitySet::UpdateVolley(float deltaTime) {
	if (!IsVolleyActive()) {
		return;
	}

	volleyTimer_ -= deltaTime;
	// 間隔が極端に短い(または0)ときは1フレームで複数発撃つ。
	while (volleyTimer_ <= 0.0f && IsVolleyActive()) {
		FireBolt(volleyAngles_[volleyIndex_], volleyTarget_);
		++volleyIndex_;
		if (chargeShotInterval_ <= 0.0f) {
			// 間隔0=全弾同時。残りをこのフレームで撃ち切る。
			continue;
		}
		volleyTimer_ += chargeShotInterval_;
	}

	if (!IsVolleyActive()) {
		CancelVolley();
	}
}

void MagicAbilitySet::CancelVolley() {
	volleyAngles_.clear();
	volleyIndex_ = 0;
	volleyTimer_ = 0.0f;
	volleyTarget_ = nullptr;
}

GameObject* MagicAbilitySet::AcquireProjectile(std::vector<GameObject*>& pool, const std::string& prefabPath) {
	// 休眠中(非アクティブ)の弾を再利用する。
	for (GameObject* pooled : pool) {
		if (pooled && !pooled->IsActive()) {
			return pooled;
		}
	}

	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || prefabPath.empty()) {
		return nullptr;
	}

	// Prefabから新規生成(Playインスタンス内のオブジェクトなので、Play停止で消える)。
	// ランタイム弾にエディタのPrefab関連付けは不要なのでlinkInstance=false。
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, prefabPath, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[MagicAbilitySet] projectile prefab load failed (" + prefabPath + "): " + result.message);
		return nullptr;
	}

	pool.push_back(result.rootObject);
	baseScales_[result.rootObject] = result.rootObject->GetTransform().scale_;
	return result.rootObject;
}

void MagicAbilitySet::ApplyScale(GameObject* projectileObject, float scale) const {
	if (!projectileObject) {
		return;
	}
	Vector3 base = {1.0f, 1.0f, 1.0f};
	auto found = baseScales_.find(projectileObject);
	if (found != baseScales_.end()) {
		base = found->second;
	}
	projectileObject->GetTransform().scale_ = base * scale;
}
