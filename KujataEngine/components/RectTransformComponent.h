#pragma once

#include "../runtime/KujataApi.h"
#include "../2d/UIRect.h"
#include "../math/Vector2.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// UI要素用の2Dトランスフォーム(UnityのRectTransform相当)。
/// アンカー/ピボット/サイズ/オフセットを持ち、親矩形から自身の矩形を算出する。
/// 座標系は左上原点(x右・y下)、アンカーは0..1(0,0=左上, 1,1=右下)。
/// </summary>
class KUJATA_API RectTransformComponent : public Component {
public:
	const char* GetTypeName() const override { return "RectTransform"; }
	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;

	/// <summary>親矩形(キャンバス単位)から自身の矩形を算出する。</summary>
	UIRect ComputeRect(const UIRect& parentRect) const;

	const Vector2& GetAnchorMin() const { return anchorMin_; }
	const Vector2& GetAnchorMax() const { return anchorMax_; }
	const Vector2& GetPivot() const { return pivot_; }
	const Vector2& GetAnchoredPosition() const { return anchoredPosition_; }
	const Vector2& GetSizeDelta() const { return sizeDelta_; }

	float GetRotationZ() const { return rotationZ_; }

	void SetAnchorMin(const Vector2& value) { anchorMin_ = value; }
	void SetAnchorMax(const Vector2& value) { anchorMax_ = value; }
	void SetPivot(const Vector2& value) { pivot_ = value; }
	void SetAnchoredPosition(const Vector2& value) { anchoredPosition_ = value; }
	void SetSizeDelta(const Vector2& value) { sizeDelta_ = value; }
	/// <summary>自分の矩形の中心まわりの回転[rad]。**子には伝播しない**(この要素の描画だけが回る)。</summary>
	void SetRotationZ(float value) { rotationZ_ = value; }

private:
	Vector2 anchorMin_ = {0.5f, 0.5f};
	Vector2 anchorMax_ = {0.5f, 0.5f};
	Vector2 pivot_ = {0.5f, 0.5f};
	Vector2 anchoredPosition_ = {0.0f, 0.0f};
	Vector2 sizeDelta_ = {100.0f, 100.0f};
	float rotationZ_ = 0.0f;
	Vector2 scale_ = {1.0f, 1.0f};
};

} // namespace KujataEngine
