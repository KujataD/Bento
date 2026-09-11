#pragma once
#include "IAbilitySet.h"
#include <components/AnimatorComponent.h>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class CharacterMotor;
class StaminaComponent;
class MagicProjectile;

/// <summary>
/// 魔法攻撃(Bishop=術師)のAbilitySet。
///
///   スロット0 = 通常: 杖を振る詠唱クリップ(Cast Clip)を再生し、Fire Delay秒後に魔法弾を Bolt Count 発。
///              円周上へ散開してから、Z注目中は対象へホーミングする。
///   スロット1 = 溜め: 溜めクリップ(Charge Clip)を再生し、Charge Fire Delay秒後に
///              **通常弾を Charge Bolt Count 発、Charge Shot Interval 秒おきに1発ずつ**撃つ(斉射)。
///              散開の向きは**毎回シャッフルする**ので、順に回らずバラバラの向きへ飛び出して見える。
///
/// スタミナ: 通常=Stamina Cost Normal[%] / 溜め=Stamina Cost Charge[%]。残量>0で出せる(消費は撃ち始めに1回)。
/// 弾はPrefabからランタイム生成し、非アクティブになったものをプールとして再利用する(Prefab読み込みは初弾のみ)。
/// 見た目・コライダー・発光はPrefabとそのMaterialアセット側で調整する。速度・ダメージなどの性能はこちらのフィールド。
/// IsBusy()は「詠唱中」と「斉射の最中」。クールダウンは含めない。
/// </summary>
class MagicAbilitySet : public IAbilitySet {
public:
	const char* GetTypeName() const override { return "MagicAbilitySet"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	bool TryUse(int slot) override;
	bool IsBusy() const override;

	/// <summary>
	/// 致命の一撃。**杖を地面へ突き立て、上空へ弾をばら撒く。**
	/// 弾はまっすぐ上へ出てからホーミングで対象へ降り注ぐ(旋回が遅いほど大きな弧を描く)。
	/// totalDamageを Critical Bolt Count で割った値が1発ぶんのダメージになるので、
	/// **弾数を変えても致命1回の総ダメージは変わらない**(剣士と揃う)。
	/// クールダウン・スタミナ・行動不能の制限は無視する(致命は既に成立しているため)。
	/// </summary>
	bool TryCritical(KujataEngine::GameObject* target, float totalDamage) override;
	/// <summary>致命の溜め中。杖の球を育てて「魔力を溜めている」ことを見せる。</summary>
	void OnCriticalWindup(float progress) override;
	/// <summary>致命の終了。溜めの球を消す。</summary>
	void OnCriticalEnd() override;

	/// <summary>
	/// 小弾を即座に発射する(ジャストガードの自動反撃用。詠唱なし・スタミナ消費なし)。
	/// targetがあればそちらへ向けて撃ち、ホーミングも付ける。
	/// </summary>
	bool FirePebble(KujataEngine::GameObject* target);

	/// <summary>
	/// 狙う相手を指定する(頭脳が毎フレーム渡す)。**弾に仰角を付けるために要る。**
	/// 体の向きは水平のままなので、これが無いと浮いている相手へ永久に届かない。
	/// nullptrならZ注目の対象へフォールバックする(プレイヤー操作は従来どおり)。
	/// </summary>
	void SetAimTarget(KujataEngine::GameObject* target) { aimTarget_ = target; }
	KujataEngine::GameObject* GetAimTarget() const { return aimTarget_; }

private:
	enum class PendingShot { None, Normal, Charge };

	/// <summary>プールから休眠中の弾を返す。無ければPrefabから新規生成する。</summary>
	KujataEngine::GameObject* AcquireProjectile(std::vector<KujataEngine::GameObject*>& pool, const std::string& prefabPath);
	/// <summary>発射位置と前方を求める。</summary>
	void GetMuzzle(KujataEngine::Vector3& outPosition, KujataEngine::Vector3& outForward) const;
	/// <summary>現在の注目対象(LockOnController)。無ければnullptr。</summary>
	KujataEngine::GameObject* FindLockOnTarget() const;

	/// <summary>
	/// 通常弾を1発撃つ。散開の向きは spreadAngleDeg で指定する。
	/// 通常攻撃も溜めの斉射も、撃つ弾そのものはこれで共通。
	/// </summary>
	void FireBolt(float spreadAngleDeg, KujataEngine::GameObject* lockOnTarget);

	void FireNormal();
	/// <summary>溜めの斉射を始める(散開角度を作ってシャッフルし、1発目を撃つ)。</summary>
	void StartVolley();
	/// <summary>斉射の残りを進める。間隔ごとに1発ずつ撃つ。</summary>
	void UpdateVolley(float deltaTime);
	/// <summary>斉射を打ち切る(被弾などで中断されたとき)。</summary>
	void CancelVolley();
	/// <summary>斉射の最中か。</summary>
	bool IsVolleyActive() const { return volleyIndex_ < volleyAngles_.size(); }

	/// <summary>
	/// 杖の先の光る球(溜めの見える化)を更新する。詠唱・溜めの進み具合に応じて0から育て、
	/// 撃ち始めたら消す。球は演出専用で当たり判定を持たない。
	/// </summary>
	void UpdateStaffOrb();
	/// <summary>杖の先の球を探す(名前で子孫から。見つからなければnullptr)。</summary>
	KujataEngine::GameObject* FindStaffOrb();

	/// <summary>ホーミングを付ける(注目中のみ)。</summary>
	void ApplyHoming(MagicProjectile* projectile, KujataEngine::GameObject* target);
	/// <summary>弾のスケールをPrefab定義値×scaleへ戻す/設定する。</summary>
	void ApplyScale(KujataEngine::GameObject* projectileObject, float scale) const;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED(projectilePrefabPath_, "Projectile Prefab");
		KUJATA_REGISTER_FLOAT_NAMED(cooldownSeconds_, "Cooldown Seconds", 0.05f, 0.0f, 10.0f);
		KUJATA_REGISTER_FLOAT_NAMED(projectileSpeed_, "Projectile Speed", 0.1f, 0.1f, 100.0f);
		KUJATA_REGISTER_FLOAT_NAMED(projectileLifetime_, "Projectile Lifetime", 0.1f, 0.1f, 30.0f);
		KUJATA_REGISTER_FLOAT_NAMED(projectileDamage_, "Projectile Damage", 1.0f, 0.0f, 1000.0f);
		KUJATA_REGISTER_FLOAT_NAMED_TIP(projectilePoise_, "Projectile Poise", 1.0f, 0.0f, 10000.0f, "弾1ヒットの体勢崩し値。");
		KUJATA_REGISTER_FLOAT_NAMED(muzzleForward_, "Muzzle Forward", 0.05f, 0.0f, 10.0f);
		KUJATA_REGISTER_FLOAT_NAMED(muzzleHeight_, "Muzzle Height", 0.05f, -10.0f, 10.0f);
		KUJATA_REGISTER_STRING_NAMED_TIP(castClipName_, "Cast Clip", "通常弾の詠唱(杖振り)クリップ名。空なら即発射。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(fireDelay_, "Fire Delay", 0.01f, 0.0f, 3.0f, "通常: 詠唱開始から発射までの秒数(杖の振り抜きに合わせる)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(chargeClipName_, "Charge Clip", "溜めの詠唱クリップ名。空なら即発射。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(chargeFireDelay_, "Charge Fire Delay", 0.01f, 0.0f, 3.0f, "溜め: 詠唱開始から1発目までの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(staminaCostNormal_, "Stamina Cost Normal", 1.0f, 0.0f, 100.0f, "通常攻撃1回のスタミナ消費[最大値比%]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(staminaCostCharge_, "Stamina Cost Charge", 1.0f, 0.0f, 100.0f, "溜め1回のスタミナ消費[最大値比%]。撃ち始めに1回だけ引く。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(homingTurnRateDeg_, "Homing Turn Rate", 1.0f, 0.0f, 720.0f, "Z注目中に弾が対象へ曲がる速さ[deg/s]。0でホーミングなし。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(maxElevationDeg_, "Max Elevation", 1.0f, 0.0f, 89.0f,
		    "弾を上下に振れる最大角[deg]。**浮いている相手を撃つために要る。**\n"
		    "体の向きは水平のままで、弾の向きだけが持ち上がる。0で従来どおり真横にしか飛ばない。");
		KUJATA_REGISTER_INT_NAMED_TIP(boltCount_, "Bolt Count", 1.0f, 1, 12,
		    "通常攻撃1回で撃つ弾の数。円周上に等間隔で散らす。");
		KUJATA_REGISTER_INT_NAMED_TIP(chargeBoltCount_, "Charge Bolt Count", 1.0f, 1, 32,
		    "溜め1回で撃つ弾の数。円周を等分した向きへ散らし、**撃つ順番はシャッフルする**。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(chargeShotInterval_, "Charge Shot Interval", 0.01f, 0.0f, 2.0f,
		    "溜めの弾と弾の間隔[秒]。固定間隔で1発ずつ撃つ。0にすると全弾同時になる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spreadRadius_, "Spread Radius", 0.05f, 0.0f, 10.0f,
		    "散開の広がり。発射直後に進行方向を軸とした円周上へこの半径まで開き、**開いたまま保つ**。0でまっすぐ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spreadSeconds_, "Spread Seconds", 0.01f, 0.0f, 3.0f,
		    "開き切るまでの時間[秒]。短いほど鋭く散り、すぐホーミングへ移る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spreadPhaseOffset_, "Spread Phase Offset", 1.0f, 0.0f, 360.0f,
		    "散開の開始角度[deg]。0なら左右へ、90なら上下へ開く。");
		KUJATA_REGISTER_STRING_NAMED_TIP(orbObjectName_, "Orb Object Name",
		    "杖の先に置いた光る球のGameObject名。子孫から名前で探す。空なら球の演出をしない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(orbSizeNormal_, "Orb Size Normal", 0.01f, 0.0f, 5.0f,
		    "通常攻撃の詠唱中に球が育つ最大スケール。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(orbSizeCharge_, "Orb Size Charge", 0.01f, 0.0f, 5.0f,
		    "溜め中に球が育つ最大スケール。通常より大きくすると溜めだと一目で分かる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stabHeightOffset_, "Stab Height Offset", 0.01f, -3.0f, 3.0f,
		    "杖を突き立てる高さ(相手の位置からの差)。**0付近にして足元を狙う。**\n"
		    "上げると相手を刺しているように見えてしまい、地面ごと貫く印象が薄れる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stabBurstStrength_, "Stab Burst Strength", 0.05f, 0.0f, 6.0f,
		    "突き立てた地点で炸裂する魔力の強さ。粒の数・初速・大きさにまとめて掛かる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stabDustStrength_, "Stab Dust Strength", 0.05f, 0.0f, 6.0f,
		    "同じ地点で舞い上がる土埃の強さ。魔力だけだと地面へ突き刺した感じが出ない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(criticalOrbSize_, "Critical Orb Size", 0.01f, 0.0f, 5.0f,
		    "致命の溜めで杖の球が育つ最大スケール。溜め攻撃より大きくすると特別に見える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pebbleDamage_, "Pebble Damage", 1.0f, 0.0f, 1000.0f, "ジャストガード反撃の小弾のダメージ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(pebblePoise_, "Pebble Poise", 1.0f, 0.0f, 10000.0f, "ジャストガード反撃の小弾の体勢崩し値。");
	}

	// 弾のPrefab(プロジェクトルート相対)。見た目・スケール・コライダー・発光Materialはここで定義する。
	KUJATA_FIELD_STRING(projectilePrefabPath_, "Prefabs/MagicBolt.prefab.json");

	// 発射間隔[s]。
	KUJATA_FIELD_FLOAT(cooldownSeconds_, 0.4f);
	// 弾速[unit/s]。
	KUJATA_FIELD_FLOAT(projectileSpeed_, 15.0f);
	// 弾の寿命[s]。
	KUJATA_FIELD_FLOAT(projectileLifetime_, 3.0f);
	// 弾のダメージ。
	KUJATA_FIELD_FLOAT(projectileDamage_, 10.0f);
	// 弾の体勢崩し値。
	KUJATA_FIELD_FLOAT(projectilePoise_, 10.0f);
	// 発射位置: 自分の前方オフセット。
	KUJATA_FIELD_FLOAT(muzzleForward_, 1.2f);
	// 発射位置: 上方向オフセット。
	KUJATA_FIELD_FLOAT(muzzleHeight_, 0.5f);
	// 詠唱クリップ名と発射タイミング。
	KUJATA_FIELD_STRING(castClipName_, "BishopCast");
	KUJATA_FIELD_FLOAT(fireDelay_, 0.3f);
	KUJATA_FIELD_STRING(chargeClipName_, "BishopCastCharge");
	KUJATA_FIELD_FLOAT(chargeFireDelay_, 0.7f);
	// スタミナ消費[%]。
	KUJATA_FIELD_FLOAT(staminaCostNormal_, 20.0f);
	KUJATA_FIELD_FLOAT(staminaCostCharge_, 50.0f);
	// ホーミング旋回速度[deg/s]。
	KUJATA_FIELD_FLOAT(homingTurnRateDeg_, 180.0f);
	// 弾を上下に振れる最大角[deg]。
	KUJATA_FIELD_FLOAT(maxElevationDeg_, 70.0f);

	// 狙う相手(頭脳が毎フレーム指定する。シリアライズしないPlay中だけの状態)。
	KujataEngine::GameObject* aimTarget_ = nullptr;
	// 通常攻撃の弾数。
	KUJATA_FIELD_INT(boltCount_, 2);
	// 溜めの弾数と間隔。
	KUJATA_FIELD_INT(chargeBoltCount_, 8);
	KUJATA_FIELD_FLOAT(chargeShotInterval_, 0.08f);
	// 散開。
	KUJATA_FIELD_FLOAT(spreadRadius_, 1.6f);
	KUJATA_FIELD_FLOAT(spreadSeconds_, 0.25f);
	KUJATA_FIELD_FLOAT(spreadPhaseOffset_, 0.0f);
	// 杖の先の光る球(溜めの見える化)。
	KUJATA_FIELD_STRING(orbObjectName_, "StaffOrb");
	KUJATA_FIELD_FLOAT(orbSizeNormal_, 0.22f);
	KUJATA_FIELD_FLOAT(orbSizeCharge_, 0.55f);
	// 致命の一撃(上空へばら撒く弾)。
	// 致命は「魔力を込めた杖を地面へ突き立てる」。弾はばら撒かない。
	KUJATA_FIELD_FLOAT(stabHeightOffset_, 0.0f);
	KUJATA_FIELD_FLOAT(stabBurstStrength_, 2.5f);
	KUJATA_FIELD_FLOAT(stabDustStrength_, 1.8f);
	KUJATA_FIELD_FLOAT(criticalOrbSize_, 0.9f);
	// ジャストガード反撃の小弾。
	KUJATA_FIELD_FLOAT(pebbleDamage_, 6.0f);
	KUJATA_FIELD_FLOAT(pebblePoise_, 10.0f);

	// 発射クールダウンの残り[s]。
	float cooldownTimer_ = 0.0f;
	// 詠唱中の発射予約と経過時間。
	PendingShot pending_ = PendingShot::None;
	float castTimer_ = 0.0f;

	// --- 溜めの斉射 ---
	// 撃つ順に並べた散開角度[deg]。StartVolleyでシャッフルして作る。
	std::vector<float> volleyAngles_;
	// 次に撃つ弾の番号。volleyAngles_.size()に達したら斉射終了。
	size_t volleyIndex_ = 0;
	// 次の1発までの残り時間[s]。
	float volleyTimer_ = 0.0f;
	// 斉射の間ロックし続ける注目対象(撃つたびに探し直すと途中で狙いがばらつくため)。
	KujataEngine::GameObject* volleyTarget_ = nullptr;
	// 致命の溜め中に球を出しているか(通常の詠唱と取り合わないための印)。
	bool criticalOrbActive_ = false;

	// 杖の先の球(OnPlayStartで解決。見つからなければnullptrのまま)。
	KujataEngine::GameObject* staffOrb_ = nullptr;

	// 散開順のシャッフル用。
	std::mt19937 random_;

	// 生成済みの弾プール(Playインスタンス内のみ。停止で消える)。
	std::vector<KujataEngine::GameObject*> projectilePool_;
	// 弾ごとのPrefab定義スケール(小弾で縮めた後、通常弾として再利用するとき戻すため)。
	std::unordered_map<KujataEngine::GameObject*, KujataEngine::Vector3> baseScales_;

	// モデル(子)のAnimator(詠唱モーション用。無くてもよい)。
	KujataEngine::AnimatorComponent* animator_ = nullptr;
	// 同じGameObjectのCharacterMotor(行動不能チェック用)。
	CharacterMotor* motor_ = nullptr;
	// 同じGameObjectのStaminaComponent(無ければ無制限)。
	StaminaComponent* stamina_ = nullptr;
};
