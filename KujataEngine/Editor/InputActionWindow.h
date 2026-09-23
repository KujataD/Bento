#pragma once

namespace KujataEngine {

// 操作(アクション)の確認ウィンドウ。
// InputActions.json の割り当てと、今のアクションの値を並べる。行のボタンから手で値を流して確かめられる。
// 値の流し込みは CUI の action.set と同じ処理(InputActionSystem::PushAction)を通る。
class InputActionWindow {
public:
	void Draw(bool* pOpen = nullptr);
};

} // namespace KujataEngine
