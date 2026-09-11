#pragma once

namespace KujataEngine {

// レンダリング確認ウィンドウ。
// 今フレームに適用されたポストエフェクト設定と、それに寄与しているVolumeの一覧を表示する。
// 値の編集はシーン上のVolumeComponent(Inspector)側で行う。
class RenderingWindow {
public:
	void Draw(bool* pOpen = nullptr);
};

} // namespace KujataEngine
