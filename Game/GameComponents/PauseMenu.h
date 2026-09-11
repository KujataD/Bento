#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// 戦闘中にゲームを止めるポーズメニュー。ボス戦シーンに1つ置く。
///
/// **勝敗が決まったあとはポーズできない**([[GameFlowManager]]に問い合わせる)。
/// 死亡演出やリトライメニューの上に重ねてしまうと、どちらの操作を受けているのか分からなくなる。
///
/// 時間は必ずUnscaledで見る。`Time::SetTimeScale(0)` で止めている最中に
/// スケール後の時間で数えると、二度と解除できなくなる。
/// </summary>
class PauseMenu : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PauseMenu"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

	/// <summary>ポーズを解除する(メニューのButtonから呼ぶ)。</summary>
	void Resume();
	/// <summary>設定画面を開く。</summary>
	void OpenSettings();
	/// <summary>設定画面を閉じてポーズメニューへ戻る。</summary>
	void CloseSettings();
	/// <summary>
	/// 操作一覧を開く。**一度閉じた説明を読み直す唯一の手段**なので、
	/// チュートリアルにも本編にも同じものを置く。
	/// </summary>
	void OpenControls();
	/// <summary>操作一覧を閉じてポーズメニューへ戻る。</summary>
	void CloseControls();
	/// <summary>タイトルへ戻る([[GameFlowManager]]へ委譲する)。</summary>
	void ReturnToTitle();

	bool IsPaused() const { return paused_; }

	/// <summary>シーン内のPauseMenuを探す(無ければnullptr)。</summary>
	static PauseMenu* FindInScene(KujataEngine::Scene* scene);

	/// <summary>
	/// そのシーンが今ポーズ中か。PauseMenuが置かれていないシーンではfalse。
	/// **戦闘時計([[GameFlowManager]])がポーズ中の時間を足さないため**に見る。
	/// </summary>
	static bool IsScenePaused(KujataEngine::Scene* scene);

private:
	void SetPaused(bool paused);
	KujataEngine::GameObject* SetObjectActive(const std::string& name, bool active);

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(pauseMenuName_, "Pause Menu",
		    "ポーズメニューのCanvasのGameObject名。**HUDよりSort Orderを大きく**すること。");
		KUJATA_REGISTER_STRING_NAMED_TIP(settingsMenuName_, "Settings Menu",
		    "設定画面のCanvasのGameObject名。空なら「設定」は使えない。\n"
		    "**ポーズメニューよりさらにSort Orderを大きく**すること(重ねて開くため)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(controlsMenuName_, "Controls Menu",
		    "操作一覧のCanvasのGameObject名。空なら「操作一覧」は使えない。\n"
		    "**ポーズメニューよりさらにSort Orderを大きく**すること(重ねて開くため)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(allowToggleClose_, "Toggle To Close",
		    "ポーズボタンをもう一度押して閉じられるようにするか。\n"
		    "**設定画面を開いている間は効かない**(閉じる順番が飛ぶため)。");
	}

	KUJATA_FIELD_STRING(pauseMenuName_, "PauseMenu");
	KUJATA_FIELD_STRING(settingsMenuName_, "SettingsMenu");
	KUJATA_FIELD_STRING(controlsMenuName_, "ControlsMenu");
	KUJATA_FIELD_BOOL(allowToggleClose_, true);

	// --- 実行時状態(シリアライズしない。Playごとに戻す) ---
	bool paused_ = false;
	bool settingsOpen_ = false;
	bool controlsOpen_ = false;
};
