#include "Camera.h"
#include "../base/DirectXCommon.h"
#include <cassert>

namespace KujataEngine {

void Camera::Initialize() {
	constBuffer_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(ConstBufferDataCamera));
	HRESULT hr = constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constMap_));
	assert(SUCCEEDED(hr));

	// ライト用カメラ
	cameraForGPUResource_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(CameraForGPU));
	hr = cameraForGPUResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraForGPUData_));
	assert(SUCCEEDED(hr));
}

void Camera::UpdateMatrix() {
	UpdateViewMatrix();
	UpdateProjectionMatrix();
	TransferConstBuffer();
}

void Camera::UpdateViewMatrix() {
	// cameraMatrix = MakeAffineMatrix(scale=1, rotate, translate)
	// viewMatrix   = Inverse(cameraMatrix)
	Matrix4x4 cameraMatrix = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, rotation_, translation_);
	matView = Inverse(cameraMatrix);
}

void Camera::UpdateProjectionMatrix() { matProjection = MakePerspectiveFovMatrix(fovAngleY, aspectRatio, nearZ, farZ); }

void Camera::TransferConstBuffer() {
	// 定数バッファへ転送
	constMap_->view = matView;
	constMap_->projection = matProjection;
	constMap_->cameraPos = translation_;
	constMap_->pad = 0.0f;
	cameraForGPUData_->worldPosition = translation_;
}


Rect Camera::GetVisibleRect(float posZ)  const {
	float distance = posZ - translation_.z;
	float halfHeight = std::tan(fovAngleY * 0.5f) * distance;
	float halfWidth = halfHeight * aspectRatio;

	Rect r = {
	    .left = translation_.x - halfWidth,
	    .right = translation_.x + halfWidth,
	    .bottom = translation_.y - halfHeight,
	    .top = translation_.y + halfHeight,
	};

	return r;
}


Rect Camera::GetVisibleRect(float distance, float blank) const {
	float halfHeight = std::tan(fovAngleY * 0.5f) * distance;
	float halfWidth = halfHeight * aspectRatio;

	Rect r = {
	    .left = -halfWidth + blank,
	    .right = halfWidth - blank,
	    .bottom = -halfHeight + blank,
	    .top = halfHeight - blank,
	};

	return r;
}

} // namespace KujataEngine