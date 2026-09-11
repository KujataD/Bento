#pragma once
#include <KujataEngine.h>

/// <summary>
/// メニューのカーソル移動音と決定音を鳴らす係。**シーンに1つ**置けばよい
/// (Canvasごとではない。選択状態はUIInputがシーンで1つだけ持っているため)。
///
/// ButtonComponent側へ音を仕込まないのは、**ボタンを増やすたびに設定を忘れる**から。
/// 「選択が変わった/決定が押された」をここで見張れば、後から足したボタンも自動で鳴る。
///
/// 時間はUnscaledで見る必要すら無い(入力の立ち上がりを見るだけ)が、
/// **ポーズ中(timeScale=0)でも鳴らないと困る**ので、時間に依存しない作りにしてある。
/// </summary>
class UiSoundPlayer : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "UiSoundPlayer"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_BOOL_NAMED_TIP(playMove_, "Play Move", "カーソルが別のボタンへ移ったときに鳴らす。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(playDecide_, "Play Decide", "決定(A / Enter / Space)で鳴らす。");
	}

	KUJATA_FIELD_BOOL(playMove_, true);
	KUJATA_FIELD_BOOL(playDecide_, true);

	// --- 実行時状態(シリアライズしない) ---
	// 前フレームの選択。**最初のフレームは鳴らさない**ため、初回は記録だけして通す。
	KujataEngine::GameObject* lastSelected_ = nullptr;
	bool primed_ = false;
};
