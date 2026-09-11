#pragma once

namespace KujataEngine {

/// <summary>
/// UIレイアウトの矩形(左上原点・ピクセル/キャンバス単位)。x,yは左上座標。
/// </summary>
struct UIRect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;

	float Left() const { return x; }
	float Top() const { return y; }
	float Right() const { return x + width; }
	float Bottom() const { return y + height; }
	bool Contains(float px, float py) const { return px >= x && px <= x + width && py >= y && py <= y + height; }
};

} // namespace KujataEngine
