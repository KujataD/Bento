#pragma once

#include "../runtime/KujataApi.h"
#include "Camera.h"
#include "Model.h"
#include "WorldTransform.h"
#include <memory>
#include <string>

namespace KujataEngine {

// 軸方向表示
class KUJATA_API AxisIndicator {
public:
	// ビューポート矩形範囲
	static const float kViewPortTopLeftX;
	static const float kViewPortTopLeftY;
	static const float kViewPortWidth;
	static const float kViewPortHeight;
	static const float kCameraDistance;

	// モデル名
	static const std::string kModelName;

	static AxisIndicator* GetInstance();

	static void SetTargetCamera(const Camera* targetCamera);

	static void SetVisible(bool isVisible);

	void Initialize();

	void Update();

	void Draw();

private:
	AxisIndicator() = default;
	~AxisIndicator() = default;
	AxisIndicator(const AxisIndicator&) = delete;
	AxisIndicator& operator=(const AxisIndicator&) = delete;

	// モデル
	std::unique_ptr<Model> model_;
	// 軸表示専用カメラ
	Camera camera_;
	// ワールドトランスフォーム
	WorldTransform worldTransform_;
	// トレースするカメラ
	const Camera* targetCamera_ = nullptr;
	// 表示フラグ
	bool isVisible_ = false;
	// 初期化済みフラグ
	bool isInitialized_ = false;
};

} // namespace KujataEngine
