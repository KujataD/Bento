#pragma once

#include "IEnemy.h"
#include <KujataEngine.h>
#include <functional>
#include <string>

/// <summary>
/// 敵のHPと体勢崩しゲージ。
/// 体勢崩し: 味方の攻撃ごとに設定された値(poiseDamage)が蓄積し、Poise Maxに達すると
/// スタン(Stun Duration秒・既定3秒)。スタン中は蓄積しない。蓄積は最後のヒットから
/// Poise Decay Delay秒後にPoise Decay Per Secondで減っていく。
/// スタン開始はOnStaggerコールバックで頭脳(GuardianBoss/HammerEnemy)へ通知し、
/// 頭脳側がモーション中断とスタン姿勢を行う。IsStaggered()の間は頭脳がBTを止める。
/// のけぞり(ジャストガード時)はFlinch()→OnFlinchで同様に通知する。
/// </summary>
class EnemyHealth : public IEnemy {
public:
	const char* GetTypeName() const override { return "HealthComponent"; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	/// <summary>
	/// HPの実体。Shared Health Owner が指定されていればそちらを、無ければ自分を返す。
	/// **残量・生死・体勢崩しの問い合わせはすべてここを経由する**ので、
	/// 部位が何個あってもHPバーは1本のまま揃う。
	/// </summary>
	EnemyHealth* Master();
	const EnemyHealth* Master() const;

	void TakeDamage(float damage);
	/// <summary>
	/// ダメージと体勢崩し値を同時に与える(味方の武器・魔法弾はこちらを使う)。
	/// attackerを渡すと、同じGameObjectにHateTableがあれば与ダメージぶんのヘイトを積む。
	/// **攻撃元はキャラのルートを渡すこと**(武器や弾そのものではヘイトの主体にならない)。
	/// </summary>
	void TakeDamage(float damage, float poiseDamage, KujataEngine::GameObject* attacker = nullptr);
	float GetHealthPercent() const;
	bool IsAlive() const;

	// --- 体勢崩し ---

	/// <summary>体勢崩し値を蓄積する。閾値到達でスタン開始(OnStagger発火)。スタン中は無視。</summary>
	void AddPoise(float poiseDamage);
	/// <summary>スタン中か。頭脳はこの間BTを止め、スタン姿勢を取る。</summary>
	bool IsStaggered() const { const EnemyHealth* m = Master(); return (m != this) ? m->IsStaggered() : stunTimer_ > 0.0f; }
	/// <summary>スタンの残り秒数。</summary>
	float GetStunRemaining() const { return stunTimer_; }
	/// <summary>スタンの全長[s](姿勢の補間用)。</summary>
	float GetStunDuration() const { return stunDuration_; }
	float GetPoisePercent() const { return poiseMax_ > 0.0f ? poise_ / poiseMax_ : 0.0f; }
	/// <summary>スタンを外部から終了する(死亡時など)。</summary>
	void ClearStagger() { stunTimer_ = 0.0f; }

	/// <summary>短いのけぞりを要求する(ジャストガード成立時など)。スタン中は無視。</summary>
	void Flinch();

	// --- IEnemy ---

	/// <summary>生きていれば狙える。</summary>
	bool IsTargetable() const override { return IsAlive(); }

	/// <summary>
	/// 注目の狙い点。Lock On Object に子オブジェクト名を入れればその位置(+オフセット)、
	/// 空ならこのGameObjectの位置(+オフセット)を狙う。敵ごとに自由に決められる。
	/// </summary>
	KujataEngine::Vector3 GetLockOnPoint() const override;

	void SetOnHealthChanged(std::function<void(float)> cb);
	void SetOnDeath(std::function<void()> cb);
	void SetOnStagger(std::function<void()> cb) { onStagger_ = std::move(cb); }
	void SetOnStaggerEnd(std::function<void()> cb) { onStaggerEnd_ = std::move(cb); }
	void SetOnFlinch(std::function<void()> cb) { onFlinch_ = std::move(cb); }

	/// <summary>
	/// **ダメージが通るたびに呼ばれる(行動は中断しない)。** 被弾のリアクションを返すためのもの。
	/// 中断を伴うのけぞりは [[SetOnFlinch]]、行動を止めるのは [[SetOnStagger]] と役割を分けてある。
	/// attacker は殴ってきた側(方向を作るのに使う。無いこともある)。
	/// 体力を共有している部位(第2形態の目など)から来た分も、**まとめ役のEnemyHealthへ届く**。
	/// </summary>
	void SetOnHit(std::function<void(KujataEngine::GameObject*, float)> cb) { onHit_ = std::move(cb); }
	/// <summary>致命を受けたときに呼ばれる(引数=のけぞる秒数)。ボス側が大きな仰け反りを演じる。</summary>
	void SetOnCritical(std::function<void(float)> cb) { onCritical_ = std::move(cb); }

	/// <summary>致命を受けたことを通知する。CriticalStrikeComponentが叩く。</summary>
	void NotifyCritical(float recoilSeconds) {
		if (onCritical_) {
			onCritical_(recoilSeconds);
		}
	}

private:
	/// <summary>
	/// 倒れた合図。土埃を上げて縮み始める。**演出を別に持つ相手(第1形態のボス)では何もしない。**
	/// </summary>
	void BeginDissolve();
	/// <summary>縮みを進め、消えきったらオブジェクトを伏せる。</summary>
	void UpdateDissolve();

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(maxHealth_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(health_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT_NAMED_TIP(poiseMax_, "Poise Max", 1.0f, 1.0f, 100000.0f,
		    "体勢崩しの閾値。味方の攻撃の体勢崩し値(通常斬り20/溜め100/ジャストガード100など)が\n"
		    "ここまで蓄積するとスタンする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(poiseDecayPerSecond_, "Poise Decay Per Second", 1.0f, 0.0f, 10000.0f,
		    "蓄積した体勢崩し値が1秒に減る量。0なら減らない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(poiseDecayDelay_, "Poise Decay Delay", 0.05f, 0.0f, 30.0f,
		    "最後に体勢崩し値を受けてから減り始めるまでの秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(stunDuration_, "Stun Duration", 0.05f, 0.0f, 30.0f,
		    "体勢崩しでスタンする秒数。スタン中はモーションが中断され、蓄積も止まる。");
		KUJATA_REGISTER_STRING_NAMED_TIP(lockOnObjectName_, "Lock On Object",
		    "Z注目の狙い点にする子孫オブジェクトの名前(例: ボスの \"Body\")。\n"
		    "空ならこのGameObject自身の位置を基準にする。動く部位を指定すれば狙い点もついていく。");
		KUJATA_REGISTER_STRING_NAMED_TIP(sharedHealthOwnerName_, "Shared Health Owner",
		    "**このHPを別のオブジェクトと共有する**ときの、本体のGameObject名。\n"
		    "指定すると、こちらが受けたダメージも体勢崩しも本体へ流し、残量も本体の値を返す。\n"
		    "\n"
		    "目と脚のように**親子関係を持たない部位**を1本のHPで束ねるためのもの。\n"
		    "親子であれば `GetComponentInParent<EnemyHealth>()` が拾ってくれるが、\n"
		    "独立したオブジェクト同士では辿る先が無いので、名前で結ぶ。空なら自分のHPを使う。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(lockOnOffset_, "Lock On Offset", 0.05f, -50.0f, 50.0f,
		    "狙い点の基準位置からのオフセット(ワールド軸)。レティクル・カメラの注視点・ホーミング弾の目標になる。\n"
		    "小型敵は頭のあたり、ボスは胴体中心など、敵ごとに自由に決められる。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(dissolveOnDeath_, "Dissolve On Death",
		    "倒れたら土埃を上げて縮みながら消えるか。\n"
		    "**演出が別に用意されている相手ではOFFにする**(第1形態のボスは撃破ではなく\n"
		    "[[Phase2Cutscene]]へ渡すので、ここで消すと演出が空振りする)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dissolveSeconds_, "Dissolve Seconds", 0.05f, 0.1f, 6.0f,
		    "縮んで消えきるまでの秒数。**短すぎると「消えた」だけになり、倒した手応えが残らない。**");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dustScale_, "Dust Scale", 0.1f, 0.1f, 30.0f,
		    "撃破時に上げる土埃の量。大きい敵ほど増やす。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(dustHeight_, "Dust Height", 0.05f, 0.0f, 20.0f,
		    "土埃を出す高さ(足元からの差)。大きい敵は胴の高さから出したほうが見栄えする。");
	}

	KUJATA_FIELD_FLOAT(maxHealth_, 100);
	KUJATA_FIELD_FLOAT(health_, 100);
	// 体勢崩しの閾値。
	KUJATA_FIELD_FLOAT(poiseMax_, 100.0f);
	// 蓄積の減衰[/s]。
	KUJATA_FIELD_FLOAT(poiseDecayPerSecond_, 20.0f);
	// 減衰開始までの待ち[s]。
	KUJATA_FIELD_FLOAT(poiseDecayDelay_, 2.0f);
	// スタン秒数。
	KUJATA_FIELD_FLOAT(stunDuration_, 3.0f);
	// HPを共有する本体のGameObject名(空=自分のHPを使う)。
	KUJATA_FIELD_STRING(sharedHealthOwnerName_, "");
	// 注目点の基準にする子孫オブジェクト名(空=自分)。
	KUJATA_FIELD_STRING(lockOnObjectName_, "");
	// 注目点のオフセット。
	KUJATA_FIELD_VECTOR3(lockOnOffset_, (KujataEngine::Vector3{0.0f, 1.0f, 0.0f}));
	KUJATA_FIELD_BOOL(dissolveOnDeath_, true);
	KUJATA_FIELD_FLOAT(dissolveSeconds_, 0.7f);
	KUJATA_FIELD_FLOAT(dustScale_, 1.6f);
	KUJATA_FIELD_FLOAT(dustHeight_, 0.8f);

	std::function<void(float)> onHealthChanged_;
	std::function<void()> onDeath_;
	std::function<void()> onStagger_;
	std::function<void()> onStaggerEnd_;
	std::function<void()> onFlinch_;
	std::function<void(KujataEngine::GameObject*, float)> onHit_;
	std::function<void(float)> onCritical_;

	// --- ランタイム状態 ---
	// 現在の体勢崩し蓄積。
	float poise_ = 0.0f;
	// 減衰開始までの残り[s]。
	float poiseDecayTimer_ = 0.0f;
	// スタン残り[s]。
	float stunTimer_ = 0.0f;

	// 消えていく最中か。
	bool dissolving_ = false;
	// 消え始めてからの経過[s]。
	float dissolveTimer_ = 0.0f;
	// 消え始めた時点のスケール。**縮めた値から縮め直さない**ために控える
	// (Playインスタンスは使い回されるので、戻さないと2回目が最初から潰れて出てくる)。
	KujataEngine::Vector3 dissolveBaseScale_ = {1.0f, 1.0f, 1.0f};
};
