#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// **行動が通らなかった理由を画面へ出す係。** HUDを持つシーンに1つ置く。
///
/// スタミナ切れ・キャラ切替の失敗・注目対象なしは、どれも「押したのに何も起きない」という
/// まったく同じ見え方になる。**操作を覚える段階では、これが一番の詰まりどころ**なので、
/// 理由を短い文で出す。理由の報告そのものは [[GameEvents]] が受け取っている。
///
/// **連打対策のクールダウンを持つ。** スタミナ切れのまま攻撃を連打されると
/// 毎フレーム報告が飛んでくるので、出し直しは Cooldown 秒に1回までにする。
/// 時間は必ずUnscaledで見る(ポーズやヒットストップで止まっている間も消えるように)。
/// </summary>
class ActionFeedback : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "ActionFeedback"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

private:
	/// <summary>文言を出す。空文字なら消す。</summary>
	void ShowText(const std::string& text);

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(textObjectName_, "Feedback Text",
		    "理由を流し込むTextのGameObject名。**HUDの見やすい位置**に置くこと。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(showSeconds_, "Show Seconds", 0.05f, 0.2f, 6.0f, "出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(cooldownSeconds_, "Cooldown", 0.05f, 0.0f, 6.0f,
		    "**同じ掲示を出し直せるまでの間隔[s]。** 連打で点滅させないための足かせ。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(playSound_, "Play Sound",
		    "掲示と同時にUIのキャンセル音を鳴らすか。音でも理由の手がかりを出したいときON。");
	}

	KUJATA_FIELD_STRING(textObjectName_, "ActionFeedbackText");
	KUJATA_FIELD_FLOAT(showSeconds_, 1.6f);
	KUJATA_FIELD_FLOAT(cooldownSeconds_, 1.0f);
	KUJATA_FIELD_BOOL(playSound_, true);

	// --- 実行時状態(シリアライズしない。Playごとに戻す) ---
	// 表示の残り[s]。0で消す。
	float showTimer_ = 0.0f;
	// 次に出せるまでの残り[s]。
	float cooldownTimer_ = 0.0f;
};
