#pragma once
#include <KujataEngine.h>

class IAbilitySet;
class CharacterMotor;
class IGuard;
class CriticalStrikeComponent;

/// <summary>
/// 入力の頭脳。ボタンを読んで体(CharacterMotor)と技(IAbilitySet)・ガード(IGuard)へ指示するだけで、
/// 自分では何も動かさない。ボタン割り当ては GameInput.h にまとめてある。
///
/// 攻撃ボタン(R2/K)は「短押し=通常(スロット0) / Charge Hold Seconds以上の長押し=溜め(スロット1)」。
/// 技のモーション中に押した場合は待たずに即スロット0を送る(コンボの先行入力)。
///
/// **致命の一撃も同じ攻撃ボタン。** 致命プロンプトが出ている相手が射程内に居れば、
/// 押した瞬間に致命が最優先で出て、その押下からは通常攻撃も溜めも出さない。
/// 入力を読むのはこのコンポーネントだけで、CriticalStrikeComponent側は入力を見ない
/// (両方で読むと1回の押下で二重に発火するため)。
/// </summary>
class Player : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "Player"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;

	/// <summary>
	/// そのGameObjectが「いま操作されているキャラ」か。
	/// [[PartyManager]] がリーダーの印を Player コンポーネントの有効/無効で付けているので、それに合わせる。
	/// **プレイヤーにだけ見せたい反応(行動が通らなかった理由など)を、AI相方で誤爆させないため**の門番。
	/// </summary>
	static bool IsControlledObject(KujataEngine::GameObject* object);
	void Update() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(chargeHoldSeconds_, "Charge Hold Seconds", 0.05f, 0.05f, 2.0f,
		    "攻撃ボタンをこの秒数以上押し続けると溜め攻撃になる。短く離せば通常攻撃。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(attackMoveScale_, "Attack Move Scale", 0.01f, 0.0f, 1.0f,
		    "攻撃モーション中に入力方向へ動ける速さの倍率(0=完全に足を止める)。\n"
		    "**少しだけ動けると格段に操作しやすくなる。** 上げすぎると振りの重さが消える。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(attackTurnScale_, "Attack Turn Scale", 0.01f, 0.0f, 1.0f,
		    "攻撃モーション中に入力方向へ向き直れる速さの倍率(0=向きを固定)。\n"
		    "いわゆる方向補正。振り出しで狙いを合わせ直せるようになる。");
	}

	// 溜め判定の長押し秒数。
	KUJATA_FIELD_FLOAT(chargeHoldSeconds_, 0.3f);
	// 攻撃中の移動・旋回の効き。
	KUJATA_FIELD_FLOAT(attackMoveScale_, 0.25f);
	KUJATA_FIELD_FLOAT(attackTurnScale_, 0.4f);

	// 同じGameObjectのCharacterMotor(操作対象の体)。
	CharacterMotor* motor_ = nullptr;
	// 同じGameObjectのIAbilitySet(攻撃手段。Melee/Magicどちらでもよい)。
	IAbilitySet* abilitySet_ = nullptr;
	// 同じGameObjectのIGuard(無ければnullptr)。
	IGuard* guard_ = nullptr;
	// 死亡中の入力遮断に使う。
	class PlayerHealth* health_ = nullptr;
	// 同じGameObjectの致命の一撃(無ければnullptr)。攻撃ボタンの押下で最優先に回す先。
	CriticalStrikeComponent* critical_ = nullptr;

	// 攻撃ボタンの状態。
	bool wasAttackPressed_ = false;
	// 押し始めてからの時間[s](長押し判定用)。
	float attackHoldTime_ = 0.0f;
	// 今回の押下はもう消費した(溜めを出した/先行入力を送った)か。離すまで再発火しない。
	bool attackConsumed_ = false;
};
