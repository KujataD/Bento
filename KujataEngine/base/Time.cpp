#include "Time.h"
#include <algorithm>


Time* Time::GetInstance() {
	static Time instance;
	return &instance;
}

void Time::Init() {
	QueryPerformanceFrequency(&frequency_);
	QueryPerformanceCounter(&prevTime_);
	startTime_ = prevTime_;

	dt_ = 0.0f;
	maxDeltaTime_ = 1.0f / 30.0f;
}

void Time::Update() {
	// 現在の時刻を取得
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);

	float realDeltaTime =
		static_cast<float>(currentTime.QuadPart - prevTime_.QuadPart) /
		static_cast<float>(frequency_.QuadPart);

	prevTime_ = currentTime;

	// DeltaTimeの暴走防止
	dt_ = std::clamp(realDeltaTime, 0.0f, maxDeltaTime_);

}

float Time::RealTimeSinceStartup() const {
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	// 長時間起動してもfloatの精度が落ちにくいよう、差をdoubleで取ってから変換する。
	return static_cast<float>(static_cast<double>(currentTime.QuadPart - startTime_.QuadPart) / static_cast<double>(frequency_.QuadPart));
}