#pragma once

namespace KujataEngine {

class Model;

// RayCastの当たり判定に使うModelを提供できるComponentが実装するインターフェース。
// 実装するComponentだけが継承し、呼び出し側はdynamic_castで能力を問い合わせる。
class IRaycastTarget {
public:
	virtual ~IRaycastTarget() = default;

	virtual const Model* GetRayCastModel() const = 0;
};

} // namespace KujataEngine
