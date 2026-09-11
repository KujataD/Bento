#pragma once
#include "../runtime/KujataApi.h"
#include <math/MathUtil.h>
#include <Windows.h>

namespace KujataEngine{

/// <summary>
/// デバッグカメラ
/// </summary>
class KUJATA_API DebugCamera {
public:
	
	void Initialize(Vector3 rotation, Vector3 translation);

	void Update();

	void UpdateViewMatrix();

	Matrix4x4 GetViewMatrix() { return matView_; }

	// ローカル回転軸
	Vector3 rotation_ = {0.0f, 0.0f, 0.0f};
	// ローカル座標
	Vector3 translation_ = {0.0f, 0.0f, 0.0f};

	// ビュー
	Matrix4x4 matView_ = MakeIdentity();
	Matrix4x4 matRot_ = MakeIdentity();
	
private:

	static inline const float kMoveSpeed = 0.1f;
	static inline const float kRotateSpeed = 0.002f;

private:
	// マウス座標
	Vector2 mousePos_ = {0.0f, 0.0f};
	Vector2 prevMousePos_ = {0.0f, 0.0f};
};
}
