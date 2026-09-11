#pragma once
#include <string>

/// <summary>
/// 「プレイヤーが何をしたか」「何に失敗したか」を1か所へ集める掲示板。
/// [[GameSession]] / [[GameSettings]] と同じ流儀の置き場(GameModule.dll内の静的変数)で、
/// シーンを作り直しても消えない。
///
/// 使い道は2つ:
///   1. **チュートリアルの課題判定**。「ジャストガードを1回成立させたか」のような
///      「動作が成立した瞬間」は、成立させた側(ガード・致命・切替)でしか分からない。
///      数えるだけならここへ1行足すだけで済み、判定側は毎フレーム値を見るだけでよい。
///   2. **行動が通らなかった理由の掲示**。スタミナ切れ・切替失敗・注目対象なしは、
///      どれも「押したのに何も起きない」という同じ見え方になる。理由を残しておき、
///      [[ActionFeedback]] が画面へ出す。
///
/// **カウンタはシーンを跨いで残る。** チュートリアルのように「このシーンでの回数」を
/// 見たいときは、開始時に ResetCounters() を呼んでから数えること。
/// </summary>
namespace GameEvents {

// --- 成立した動作の回数 ---

/// <summary>ジャストガードが成立した回数(剣士の盾・術師のバリアの両方)。</summary>
inline int& JustGuardCountRef() {
	static int count = 0;
	return count;
}

/// <summary>致命の一撃を決めた回数。</summary>
inline int& CriticalCountRef() {
	static int count = 0;
	return count;
}

/// <summary>操作キャラを切り替えた回数。</summary>
inline int& SwapCountRef() {
	static int count = 0;
	return count;
}

/// <summary>Z注目で対象を掴んだ回数。</summary>
inline int& LockOnCountRef() {
	static int count = 0;
	return count;
}

/// <summary>回数を全部ゼロに戻す。**シーンの開始時に呼ぶ。**</summary>
inline void ResetCounters() {
	JustGuardCountRef() = 0;
	CriticalCountRef() = 0;
	SwapCountRef() = 0;
	LockOnCountRef() = 0;
}

// --- 行動が通らなかった理由 ---

/// <summary>直前に握り潰された行動の理由。</summary>
enum class Failure {
	None = 0,
	NoStamina,      // スタミナが足りず技が出なかった
	SwapFailed,     // キャラ切替が通らなかった(相方が死亡中・硬直中・クールダウン中)
	NoLockOnTarget, // 注目しようとしたが対象がいなかった
};

inline Failure& LastFailureRef() {
	static Failure failure = Failure::None;
	return failure;
}

/// <summary>直前の失敗が積まれてからの経過[s]。表示側が数え、報告側は0へ戻すだけ。</summary>
inline float& FailureAgeRef() {
	static float age = 0.0f;
	return age;
}

/// <summary>
/// 行動が通らなかったことを報告する。**同じ理由の連打は上書きするだけ**なので、
/// 押しっぱなしでも掲示が増殖しない。表示のクールダウンは出す側([[ActionFeedback]])が持つ。
/// </summary>
inline void ReportFailure(Failure failure) {
	LastFailureRef() = failure;
	FailureAgeRef() = 0.0f;
}

/// <summary>掲示を消す(読み終えた側が呼ぶ)。</summary>
inline void ClearFailure() {
	LastFailureRef() = Failure::None;
	FailureAgeRef() = 0.0f;
}

/// <summary>理由の日本語表記。空文字ならNone。</summary>
inline std::string FailureText(Failure failure) {
	switch (failure) {
	case Failure::NoStamina:
		return "スタミナが足りない";
	case Failure::SwapFailed:
		return "今は入れ替えられない";
	case Failure::NoLockOnTarget:
		return "狙える相手がいない";
	default:
		return std::string();
	}
}

} // namespace GameEvents
