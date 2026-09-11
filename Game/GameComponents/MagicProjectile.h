#pragma once
#include <KujataEngine.h>
#include <unordered_map>

/// <summary>
/// 魔法弾(MagicAbilitySetがランタイム生成する)。
/// Fire()で発射され、直進しながら敵(EnemyHealth持ち)に接触するとダメージを与えて消える。
/// 寿命切れでも消える。「消える」はSetActive(false)で、オブジェクトはプールとして再利用される
/// (Update中のオブジェクト破棄を避けるため。MagicAbilitySet側が非アクティブ弾を再発射する)。
/// 味方(PlayerHealth持ち)には当たらない(フレンドリーファイアなし)。
///
/// 追加の振る舞い:
///   - ホーミング: SetHomingTarget で対象を与えると、Turn Rate[deg/s]の範囲で対象へ曲がる(Z注目中の通常弾)。
///   - 貫通: Fire の pierce=true で、当たっても消えず hitInterval ごとに再ヒットする(溜め弾・衛星弾)。
///   - 衛星: FireAsSatellite で親弾の周りを公転しながら追従する。親が消えたら自分も消える。
/// </summary>
class MagicProjectile : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "MagicProjectile"; }
	bool AllowMultiple() const override { return false; }

	void Update() override;
	void OnTriggerStay(KujataEngine::ColliderComponent* other) override;

	/// <summary>
	/// 発射する(位置は呼び出し側がTransformへ設定済みであること)。
	/// pierce=trueなら当たっても消えず、hitIntervalごとに同じ敵へ再ヒットする。
	/// </summary>
	void Fire(const KujataEngine::Vector3& direction, float speed, float lifetime, float damage, float poiseDamage = 0.0f,
	    bool pierce = false, float hitInterval = 0.5f);

	/// <summary>
	/// 発射直後の「散開」を設定する。進行方向を軸にした円周方向へ **片道で開き、そのまま保つ**。
	/// 複数弾に別々のangleDegを与えると、輪を描くように散らばる。
	///
	/// **軸へ戻さないのが要点**。戻すと「散って集まるだけ」の動きになり、
	/// 散開した位置からホーミングで敵へ収束する、という軌道にならない。
	/// 開き切ったあとは横のズレを保ったまま、ホーミング(または直進)が弾を導く。
	///
	/// 半径は seconds 秒かけて 0 から radius まで開く(立ち上がりが速いEaseOut)。0秒または半径0で散開なし。
	/// Fire()の直後に呼ぶこと(Fire()が散開状態をリセットするため)。
	/// </summary>
	void SetSpread(float radius, float seconds, float angleDeg);

	/// <summary>
	/// 親弾の周りを公転する衛星として発射する。位置は親弾基準で毎フレーム決めるので、呼び出し側は位置を設定しなくてよい。
	/// phaseDegは公転の初期位相(2発なら0と180)。親弾が非アクティブになったら自分も消える。常に貫通(再ヒットあり)。
	/// </summary>
	void FireAsSatellite(KujataEngine::GameObject* parent, float radius, float orbitSpeedDeg, float phaseDeg, float damage, float poiseDamage,
	    float hitInterval);

	/// <summary>
	/// この弾を撃ったキャラのルート。ヘイトの積み先になる。
	/// **弾そのものではなく撃った本人を渡すこと**(弾を渡すと敵が弾を狙い始める)。
	/// </summary>
	void SetAttacker(KujataEngine::GameObject* attacker) { attacker_ = attacker; }

	/// <summary>ホーミング対象を設定する(nullptrで解除)。aimHeightは対象位置へ足す高さ。</summary>
	void SetHomingTarget(KujataEngine::GameObject* target, float aimHeight, float turnRateDeg);

	/// <summary>進行方向(正規化済み)。衛星の公転面の基準に使う。</summary>
	const KujataEngine::Vector3& GetDirection() const { return direction_; }
	/// <summary>飛行中か(寿命が残っているか)。</summary>
	bool IsAlive() const { return lifetime_ > 0.0f || satellite_; }

private:
	// 撃った本人(ヘイトの積み先)。プールで使い回すのでFireのたびに設定し直す。
	KujataEngine::GameObject* attacker_ = nullptr;
	/// <summary>ホーミング: 進行方向を対象へ曲げる。</summary>
	void SteerTowardsTarget(float deltaTime);
	/// <summary>散開: 直進位置に対する横方向オフセットを更新し、その差分(このフレームで加える移動量)を返す。</summary>
	KujataEngine::Vector3 ConsumeSpreadDelta(float deltaTime);
	/// <summary>衛星: 親弾基準の位置へ置く。親が消えていたらfalse。</summary>
	bool UpdateSatellite(float deltaTime);
	/// <summary>消える(非アクティブ化)。</summary>
	void Expire();
	/// <summary>命中演出。地形ヒット・敵ヒット・蘇生の3経路から呼ぶ。</summary>
	void SpawnHitEffect();

private:
	// 進行方向(正規化済み)。
	KujataEngine::Vector3 direction_ = {0.0f, 0.0f, 1.0f};
	float speed_ = 15.0f;
	// 残り寿命[s]。0以下で消える。
	float lifetime_ = 0.0f;
	float damage_ = 10.0f;
	// 1ヒットの体勢崩し値。
	float poiseDamage_ = 0.0f;

	// --- 貫通 ---
	bool pierce_ = false;
	float hitInterval_ = 0.5f;
	// 貫通時の対象ごとの再ヒットまでの残り時間。
	std::unordered_map<KujataEngine::GameObject*, float> hitCooldowns_;

	// --- 散開(発射直後に円周上へ膨らんでから正面へ戻る) ---
	// 開き切ったときの半径。0で散開なし。
	float spreadRadius_ = 0.0f;
	// 開き切るまでの時間[s]。
	float spreadSeconds_ = 0.0f;
	// 円周上のどの向きへ開くか[rad]。弾ごとにずらすと輪になる。
	float spreadAngle_ = 0.0f;
	// 散開開始からの経過[s]。
	float spreadElapsed_ = 0.0f;
	// 前フレームに散開で加えたオフセット。差分だけ動かすことで直進成分と両立させる。
	KujataEngine::Vector3 spreadOffset_ = {0.0f, 0.0f, 0.0f};

	// --- ホーミング ---
	KujataEngine::GameObject* homingTarget_ = nullptr;
	float homingAimHeight_ = 1.0f;
	float homingTurnRateDeg_ = 0.0f;

	// --- 衛星 ---
	bool satellite_ = false;
	KujataEngine::GameObject* satelliteParent_ = nullptr;
	float satelliteRadius_ = 1.0f;
	float satelliteOrbitSpeedDeg_ = 180.0f;
	float satelliteAngleDeg_ = 0.0f;
};
