#pragma once

namespace KujataEngine {

// パフォーマンス監視ウィンドウ。FPS/フレーム時間/その履歴グラフに加え、
// Scene/Gameビューが描画中かスキップ中か・各RenderTextureの解像度を表示する。
// 2画面化(2パス描画)の負荷や、非表示ビューのスキップが効いているかを確認するのに使う。
class PerformanceWindow {
public:
	void Draw(bool* pOpen = nullptr);

private:
	static const int kHistoryCount = 120;
	float frameTimeHistoryMs_[kHistoryCount] = {};
	int historyOffset_ = 0;
};

} // namespace KujataEngine
