#pragma once
#include <KujataEngine.h>
#include <string>
#include <vector>

namespace KujataEngine {
class ImageComponent;
class TextComponent;
}

/// <summary>
/// 1回の挑戦の勝敗を見張り、演出とリトライメニューまでを進める司令塔。ボス戦シーンに1つ置く。
///
/// - **クリア**: Boss Name のGameObjectが持つ HealthComponent(EnemyHealth) が死んだら。
/// - **ゲームオーバー**: PartyManagerが指す2人の PlayerHealth が**両方**倒れたら。
///   片方だけなら相方を叩いて起こせる([[death-and-revive]])ので、まだ負けではない。
///
/// 進行はすべて**Unscaled**で数える。メニューを出す間 `Time::SetTimeScale(0)` でゲームを止めるため。
/// 止めた時間を戻す責任はこちらにあるので、OnPlayStartとシーン切り替えの直前で必ず1.0へ戻す。
/// </summary>
class GameFlowManager : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "GameFlowManager"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	// リトライメニューのButton.onClickから呼ぶ。
	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

	/// <summary>同じシーンを読み直して再挑戦する。</summary>
	void Retry();
	/// <summary>タイトルへ戻る。</summary>
	void ReturnToTitle();
	/// <summary>
	/// 指定のシーンから再挑戦する(再開点の選択)。空ならRetryと同じ。
	/// ボタンからは RetryFromStart / RetryFromPhase2 で呼ぶ。
	/// </summary>
	void RetryFrom(const std::string& sceneName);

private:
	enum class State {
		Playing,
		DeathDelay,   // 倒れてから文字が出るまでの間
		DeathMessage, // "YOU DIED"表示中
		DeathFadeOut, // 暗転中(黒で覆いきるのを待つ)
		GameOverMenu, // リトライメニュー操作中(ゲームは停止)
		ClearDelay,    // ボスが倒れてから文字が出るまでの間
		ClearMessage,  // 撃破表示中
		ClearFadeOut,  // リザルトへ移る前の暗転
		PhaseTransition, // 次の形態への繋ぎ(Phase2Cutsceneが進行を持っている間)
		ResultMenu,    // 戦績表示中(ゲームは停止)
		Leaving,       // シーン切り替えを発行済み
	};

public:
	/// <summary>まだ勝敗が決まっていない(=ポーズしてよい)か。[[PauseMenu]]が見る。</summary>
	bool IsInGameplay() const { return state_ == State::Playing; }

	/// <summary>シーン内のGameFlowManagerを探す(無ければnullptr)。</summary>
	static GameFlowManager* FindInScene(KujataEngine::Scene* scene);

private:

	bool IsBossDefeated();
	bool IsPartyWiped() const;

	/// <summary>
	/// 1試合の結果をログへ書き、Auto Retryなら次の試合を始める。
	/// **勝率はここでしか分からない。** 集計はシーンを跨いで残る GameSession に置く。
	/// </summary>
	void RecordOutcome(bool won);

	/// <summary>リザルトの数字(撃破タイム・リトライ回数)をTextへ流し込む。</summary>
	void FillResultTexts();
	/// <summary>名前で探したGameObjectのTextを差し替える(無ければ何もしない)。</summary>
	void SetText(const std::string& objectName, const std::string& text);

	/// <summary>rootとその子孫のImage/Textの色を控える(フェード演出の基準にする)。</summary>
	void CaptureFadeTargets(KujataEngine::GameObject* root);
	/// <summary>控えた色のαにratioを掛ける。</summary>
	void ApplyFadeRatio(float ratio);
	/// <summary>名前でGameObjectを探して表示/非表示を切り替える。</summary>
	KujataEngine::GameObject* SetObjectActive(const std::string& name, bool active);

	/// <summary>フェード対象1つ分。ImageかTextのどちらか一方が入る。</summary>
	struct FadeTarget {
		KujataEngine::ImageComponent* image = nullptr;
		KujataEngine::TextComponent* text = nullptr;
		KujataEngine::Vector4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
	};

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bossName_, "Boss Name",
		    "クリア判定に使うボスのGameObject名。**HealthComponentを持つオブジェクト**を指すこと。\n"
		    "見つからない間はクリア判定を行わない(名前を間違えても即クリアにはならない)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(deathMessageName_, "Death Message", "死亡時に表示するGameObject名(既定は非表示にされる)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(clearMessageName_, "Clear Message", "撃破時に表示するGameObject名(既定は非表示にされる)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(retryMenuName_, "Retry Menu",
		    "リトライメニューのCanvasのGameObject名。**HUDのCanvasよりSort Orderを大きく**すること\n"
		    "(パッドのフォーカスは最前面のCanvasだけが受け取るため)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(resultMenuName_, "Result Menu",
		    "撃破後の戦績画面のCanvasのGameObject名。空ならそのままタイトルへ戻る(従来の挙動)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(resultTimeName_, "Result Time Text", "撃破タイムを流し込むTextのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(resultRetryName_, "Result Retry Text", "リトライ回数を流し込むTextのGameObject名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(titleSceneName_, "Title Scene", "タイトルへ戻るときのシーン名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(retrySceneName_, "Retry Scene", "再挑戦で読み直すシーン名。空なら現在のシーン。");
		KUJATA_REGISTER_STRING_NAMED_TIP(firstPhaseSceneName_, "First Phase Scene",
		    "「最初から」で読み直すシーン名。**再開点を選ばせるため**にリトライメニューのボタンから使う。");
		KUJATA_REGISTER_STRING_NAMED_TIP(secondPhaseSceneName_, "Second Phase Scene",
		    "「第2形態から」で読み直すシーン名。空ならそのボタンは通常のRetryと同じ動きになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(deathDelaySeconds_, "Death Delay", 0.05f, 0.0f, 10.0f,
		    "倒れてから文字が出るまでの間[s]。ここが短いと死んだ実感の前に画面が切り替わる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(deathMessageSeconds_, "Death Message Seconds", 0.05f, 0.0f, 20.0f, "死亡文字を出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(deathFadeOutSeconds_, "Death Fade Out", 0.05f, 0.0f, 10.0f,
		    "死亡文字のあと、リトライメニューへ移る前に暗転させる秒数。\n"
		    "**暗転しきってから**メニューへ差し替えるので、闘技場が消える瞬間は見えない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(deathFadeInSeconds_, "Death Fade In", 0.05f, 0.0f, 10.0f,
		    "メニューへ差し替えたあと、覆いを開けるまでの秒数。\n"
		    "**リトライメニューの背景は不透明にしておくこと**(半透明だと明転で闘技場が透けて戻る)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(clearDelaySeconds_, "Clear Delay", 0.05f, 0.0f, 10.0f, "ボス撃破から文字が出るまでの間[s]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(clearMessageSeconds_, "Clear Message Seconds", 0.05f, 0.0f, 20.0f, "撃破文字を出しておく秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(messageFadeSeconds_, "Message Fade Seconds", 0.05f, 0.0f, 10.0f, "文字がふわっと出るまでの秒数。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(autoRetry_, "Auto Retry",
		    "**勝敗が決まったら自動で再挑戦する(検証用)。** メニューを待たずに次の試合へ進み、\n"
		    "勝敗と所要時間をログへ書き出す。PartyManagerの Auto Battle と組み合わせて勝率を測るためのもので、\n"
		    "**通常プレイでは必ずOFF**にしておくこと。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(autoRetryDelay_, "Auto Retry Delay", 0.05f, 0.0f, 10.0f,
		    "自動再挑戦までの待ち[s]。0にすると結果が読めないので少しは置く。");
	}

	KUJATA_FIELD_STRING(bossName_, "GuardianSpline");
	KUJATA_FIELD_STRING(deathMessageName_, "DeathMessage");
	KUJATA_FIELD_STRING(clearMessageName_, "ClearMessage");
	KUJATA_FIELD_STRING(retryMenuName_, "RetryMenu");
	KUJATA_FIELD_STRING(resultMenuName_, "ResultMenu");
	KUJATA_FIELD_STRING(resultTimeName_, "ResultTimeValue");
	KUJATA_FIELD_STRING(resultRetryName_, "ResultRetryValue");
	KUJATA_FIELD_STRING(titleSceneName_, "TitleScene");
	KUJATA_FIELD_STRING(retrySceneName_, "");
	// 「最初から」「第2形態から」で読むシーン。
	KUJATA_FIELD_STRING(firstPhaseSceneName_, "BossArenaScene");
	KUJATA_FIELD_STRING(secondPhaseSceneName_, "BossPhase2Scene");
	KUJATA_FIELD_FLOAT(deathDelaySeconds_, 1.0f);
	KUJATA_FIELD_FLOAT(deathMessageSeconds_, 2.6f);
	KUJATA_FIELD_FLOAT(deathFadeOutSeconds_, 0.8f);
	KUJATA_FIELD_FLOAT(deathFadeInSeconds_, 0.5f);
	KUJATA_FIELD_FLOAT(clearDelaySeconds_, 1.5f);
	KUJATA_FIELD_FLOAT(clearMessageSeconds_, 3.2f);
	KUJATA_FIELD_FLOAT(messageFadeSeconds_, 0.9f);
	// 勝敗が決まったら自動で再挑戦する(勝率計測用)。
	KUJATA_FIELD_BOOL(autoRetry_, false);
	// 自動再挑戦までの待ち[s]。
	KUJATA_FIELD_FLOAT(autoRetryDelay_, 1.5f);

	// --- 実行時状態(シリアライズしない。OnPlayStartで必ず戻す) ---
	State state_ = State::Playing;
	float timer_ = 0.0f;
	// 戦闘が始まってからボスを倒すまでの秒数。**Playing中だけ進める**ので、
	// 死亡演出やポーズで止まっている間は加算されない。
	float battleSeconds_ = 0.0f;
	class EnemyHealth* bossHealth_ = nullptr;
	std::vector<FadeTarget> fadeTargets_;
};
