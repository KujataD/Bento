#pragma once
#include <windows.h>

#include "../runtime/KujataApi.h"

// 参考資料:
// https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps
// https://learn.microsoft.com/en-us/windows/win32/dxtecharts/game-timing-and-multicore-processors

class KUJATA_API Time {
public:
	
	static Time* GetInstance();
	/// <summary>
	/// 前フレームからの経過秒。**時間スケールが掛かった値**なので、
	/// ゲーム内の動き(移動・アニメ・タイマー)は基本これを使う。
	/// </summary>
	static float GetDeltaTime() { return GetInstance()->DeltaTime(); }

	/// <summary>
	/// スケールを掛けない実時間の経過秒。
	/// **ヒットストップやスロー自体の制御・カメラ・UIはこちらを使う**
	/// (スケールした値で数えると、止めている間タイマーも止まって復帰できない)。
	/// </summary>
	static float GetUnscaledDeltaTime() { return GetInstance()->UnscaledDeltaTime(); }

	/// <summary>
	/// 時間の進む速さ。1=等速 / 0=停止(ヒットストップ) / 0.2=スロー。
	/// **元へ戻す責任は設定した側にある**(戻し忘れるとゲームが止まったままになる)。
	/// </summary>
	static void SetTimeScale(float scale) { GetInstance()->timeScale_ = (scale < 0.0f) ? 0.0f : scale; }
	static float GetTimeScale() { return GetInstance()->timeScale_; }

	void Init();
	void Update();
	float DeltaTime() const { return dt_ * timeScale_; }
	float UnscaledDeltaTime() const { return dt_; }

private:
	Time() = default;
	~Time() = default;
	Time(const Time&) = delete;
	const Time& operator=(const Time&) = delete;

private:
	float dt_;

	// 時間の進む速さ(1=等速)。ヒットストップやスローで一時的に変える。
	float timeScale_ = 1.0f;

	float maxDeltaTime_;

	// 周波数取得
	LARGE_INTEGER frequency_;

	// 開始時刻を取得
	LARGE_INTEGER prevTime_;

};