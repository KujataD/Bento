#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// 通り抜けると1回だけ発火する進行トリガー。**トリガーColliderと同じGameObjectに付ける**。
///
/// - Next Scene が空: [TutorialManager] にチュートリアルのポップアップを出させる
/// - Next Scene が入っている: 暗転してそのシーンへ移る(ステージの出口として使う)
///
/// 判定は通過したGameObjectのTagで行う。パーティは2人とも `Ally` なので、既定はそれ。
/// ただし **発火するのは操作中のリーダーだけ**([[PartyManager]]に問い合わせる)。
/// タグだけで見ると、後ろを付いてくるAI相方が先に踏んで説明が流れてしまう。
///
/// **1回で閉じる**(fired_)。ポップアップを閉じたあと同じ場所を通っても再発火しない。
///
/// Require Character を入れると、そのキャラを操作しているときだけ発火する。
/// 剣士と術師で覚えることが違うので、**同じ場所に2つ重ねて置き、片方ずつ担当させる**のが基本形。
/// </summary>
class TutorialTrigger : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "TutorialTrigger"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnTriggerEnter(KujataEngine::ColliderComponent* other) override;

private:
	/// <summary>通ったのが「今操作しているリーダー」か。</summary>
	bool IsLeader(KujataEngine::GameObject* visitor) const;
	/// <summary>Require Character の条件を満たしているか(空なら常に満たす)。</summary>
	bool MatchesCharacter(KujataEngine::GameObject* visitor) const;

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(title_, "Title", "ポップアップの見出し。");
		KUJATA_REGISTER_STRING_NAMED(line0_, "Line 0");
		KUJATA_REGISTER_STRING_NAMED(line1_, "Line 1");
		KUJATA_REGISTER_STRING_NAMED(line2_, "Line 2");
		KUJATA_REGISTER_STRING_NAMED(line3_, "Line 3");
		KUJATA_REGISTER_STRING_NAMED(line4_, "Line 4");
		KUJATA_REGISTER_STRING_NAMED(line5_, "Line 5");
		KUJATA_REGISTER_STRING_NAMED_TIP(nextScene_, "Next Scene",
		    "入れるとポップアップではなくシーン移動になる(ステージの出口)。空ならポップアップ。");
		KUJATA_REGISTER_STRING_NAMED_TIP(requireTag_, "Require Tag",
		    "このTagを持つGameObjectが通ったときだけ発火する。パーティは2人とも `Ally`。");
		KUJATA_REGISTER_STRING_NAMED_TIP(requireCharacter_, "Require Character",
		    "**このGameObject名のキャラを操作しているときだけ**発火する(例: `Pawn` / `Bishop`)。\n"
		    "空ならどちらでも発火する。剣士と術師で内容が違う説明は、同じ場所へ2つ重ねて置き分ける。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(leaderOnly_, "Leader Only",
		    "**操作中のリーダーが通ったときだけ**発火する。\n"
		    "OFFにするとAI相方が先に踏んで説明を流してしまうので、基本はONのまま。");
		KUJATA_REGISTER_STRING_NAMED_TIP(objective_, "Objective",
		    "説明を閉じたあとに出す課題。空なら課題なし(読むだけで次へ進める)。\n"
		    "`DefeatEnemies` / `JustGuard` / `Critical` / `SwapCharacter` / `LockOn` のいずれか。\n"
		    "**達成した課題の数が出口の鍵**になる([[TutorialManager]]の Required Objectives)。");
		KUJATA_REGISTER_INT_NAMED_TIP(objectiveCount_, "Objective Count", 1.0f, 1, 20,
		    "課題の必要回数。`DefeatEnemies` では使わない(出ている敵を全部倒すのが条件)。");
	}

	KUJATA_FIELD_STRING(title_, "操作");
	KUJATA_FIELD_STRING(line0_, "");
	KUJATA_FIELD_STRING(line1_, "");
	KUJATA_FIELD_STRING(line2_, "");
	KUJATA_FIELD_STRING(line3_, "");
	KUJATA_FIELD_STRING(line4_, "");
	KUJATA_FIELD_STRING(line5_, "");
	KUJATA_FIELD_STRING(nextScene_, "");
	KUJATA_FIELD_STRING(requireTag_, "Ally");
	KUJATA_FIELD_STRING(requireCharacter_, "");
	KUJATA_FIELD_BOOL(leaderOnly_, true);
	KUJATA_FIELD_STRING(objective_, "");
	KUJATA_FIELD_INT(objectiveCount_, 1);

	// --- 実行時状態(シリアライズしない。Playごとに戻す) ---
	bool fired_ = false;
};
