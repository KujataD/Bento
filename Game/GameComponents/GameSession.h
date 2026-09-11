#pragma once
#include <KujataEngine.h>
#include <string>

/// <summary>
/// シーンをまたいで持ち回すゲーム進行の状態。PartySelectionと同じ流儀の置き場
/// (GameModule.dll内の静的変数なので、シーンを丸ごと作り直しても消えない)。
///
/// ローディング画面は「LoadingSceneへ切り替え → LoadingScreenComponentが本命のシーンへ切り替える」
/// という2段構えで実現する。行き先はここに預ける。
/// </summary>
namespace GameSession {

/// <summary>ローディング画面の後に読み込むシーン名。</summary>
inline std::string& NextSceneRef() {
	static std::string nextScene;
	return nextScene;
}

/// <summary>
/// キャラ選択で選ばれた操作キャラ名。**PartySelectionと違い消費しない**ので、
/// チュートリアル→ボス戦やリトライでシーンを跨いでも選択が保たれる。
/// タイトルへ戻るとき(ResetProgress)にクリアする。
/// </summary>
inline std::string& LeaderNameRef() {
	static std::string leaderName;
	return leaderName;
}

/// <summary>今回の挑戦で何回リトライしたか(演出やデバッグ表示用)。</summary>
inline int& RetryCountRef() {
	static int retryCount = 0;
	return retryCount;
}

/// <summary>
/// 自動戦闘の通算成績(勝ち/試合数)。**勝率の計測はこれでしか取れない。**
/// シーンを読み直しても消えないよう、他の進行状態と同じくGameModule内の静的変数に置く。
/// </summary>
inline int& AutoBattleWinsRef() {
	static int wins = 0;
	return wins;
}
inline int& AutoBattleRunsRef() {
	static int runs = 0;
	return runs;
}

/// <summary>行き先を取り出してクリアする(未設定なら空文字)。</summary>
inline std::string ConsumeNextScene() {
	std::string name = NextSceneRef();
	NextSceneRef().clear();
	return name;
}

/// <summary>
/// ローディング画面を経由してシーンを切り替える。
/// 実際の読み込み(ChangeScene)は同期ブロッキングなので、LoadingScreenComponentが
/// 「画面が完全に暗転したフレーム」で呼ぶ。こうするとロードのカクつきが黒画面に隠れる。
/// </summary>
inline void LoadSceneWithLoading(const std::string& targetScene, const std::string& loadingSceneName = "LoadingScene") {
	NextSceneRef() = targetScene;
	KujataEngine::ChangeScene(loadingSceneName);
}

/// <summary>ローディングを挟まずに直接切り替える(軽いシーン同士の移動用)。</summary>
inline void LoadSceneImmediate(const std::string& targetScene) {
	NextSceneRef().clear();
	KujataEngine::ChangeScene(targetScene);
}

/// <summary>タイトルへ戻るときなど、1回の挑戦が終わったところで呼ぶ。</summary>
inline void ResetProgress() {
	RetryCountRef() = 0;
	LeaderNameRef().clear();
}

} // namespace GameSession
