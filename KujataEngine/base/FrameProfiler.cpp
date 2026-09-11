#include "FrameProfiler.h"

#include <Windows.h>
#include <format>

#include "Logger.h"

namespace KujataEngine {

namespace {

// DLLエクスポートクラスにSTL/配列メンバを持たせるとC4251が出るため、
// 状態はLoggerと同様にこの翻訳単位ローカルのstaticへ保持する。
constexpr int kCount = FrameProfiler::kSectionCount;
constexpr float kSmoothing = 0.05f;      // 平滑化係数(EMA)
constexpr uint64_t kLogInterval = 300;   // このフレーム数ごとにログへ書き出す

const char* kSectionNames[kCount] = {
    "SceneUpdate", "Collision", "EditorUI", "SceneView", "GameView", "ImGuiRender", "PresentWait",
};

int64_t qpcFrequency_ = 0;
int64_t beginTicks_[kCount] = {};
double currentMs_[kCount] = {};  // 今フレームの累積
float lastMs_[kCount] = {};      // 前フレームの確定値
float smoothedMs_[kCount] = {};  // 表示用平滑化値
int64_t frameBeginTicks_ = 0;
float frameMs_ = 0.0f;
float smoothedFps_ = 0.0f;
uint64_t frameCount_ = 0;

int64_t Now() {
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}

double TicksToMs(int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(qpcFrequency_); }

} // namespace

void FrameProfiler::NewFrame() {
	if (qpcFrequency_ == 0) {
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		qpcFrequency_ = frequency.QuadPart;
		frameBeginTicks_ = Now();
		return;
	}

	const int64_t now = Now();
	frameMs_ = static_cast<float>(TicksToMs(now - frameBeginTicks_));
	frameBeginTicks_ = now;

	const float fps = (frameMs_ > 0.0001f) ? 1000.0f / frameMs_ : 0.0f;
	smoothedFps_ = (smoothedFps_ == 0.0f) ? fps : smoothedFps_ + (fps - smoothedFps_) * kSmoothing;

	for (int i = 0; i < kCount; i++) {
		lastMs_[i] = static_cast<float>(currentMs_[i]);
		smoothedMs_[i] = smoothedMs_[i] + (lastMs_[i] - smoothedMs_[i]) * kSmoothing;
		currentMs_[i] = 0.0;
		beginTicks_[i] = 0;
	}

	frameCount_++;
	if (frameCount_ % kLogInterval == 0) {
		std::string line = std::format("[Perf] fps={:.1f} frame={:.2f}ms |", smoothedFps_, 1000.0f / (smoothedFps_ > 0.0001f ? smoothedFps_ : 1.0f));
		for (int i = 0; i < kCount; i++) {
			line += std::format(" {}={:.2f}", kSectionNames[i], smoothedMs_[i]);
		}
		Logger::Log(line);
	}
}

void FrameProfiler::Begin(Section section) { beginTicks_[section] = Now(); }

void FrameProfiler::End(Section section) {
	if (beginTicks_[section] == 0) {
		return;
	}
	// 同一フレーム内で複数回呼ばれても累積する。
	currentMs_[section] += TicksToMs(Now() - beginTicks_[section]);
	beginTicks_[section] = 0;
}

float FrameProfiler::GetLastMs(Section section) { return lastMs_[section]; }

float FrameProfiler::GetSmoothedMs(Section section) { return smoothedMs_[section]; }

float FrameProfiler::GetFrameMs() { return frameMs_; }

float FrameProfiler::GetSmoothedFps() { return smoothedFps_; }

const char* FrameProfiler::GetName(Section section) { return kSectionNames[section]; }

} // namespace KujataEngine
