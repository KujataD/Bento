#include "AxisIndicator.h"
#include "../base/DirectXCommon.h"
#include "../base/WinApp.h"
#include "../math/MathUtil.h"
#include <cassert>

namespace KujataEngine {

	// ------------------------------------------
	// 定数群定義
	// ------------------------------------------
	const float AxisIndicator::kViewPortWidth = 160.0f;
	const float AxisIndicator::kViewPortHeight = 160.0f;
	const float AxisIndicator::kViewPortTopLeftX = static_cast<float>(WinApp::kWindowWidth) - AxisIndicator::kViewPortWidth - 16.0f;
	const float AxisIndicator::kViewPortTopLeftY = 16.0f;
	const float AxisIndicator::kCameraDistance = 8.0f;

	const std::string AxisIndicator::kModelName = "axis";

	AxisIndicator* AxisIndicator::GetInstance() {
		static AxisIndicator instance;
		return &instance;
	}

	void AxisIndicator::SetTargetCamera(const Camera* targetCamera) { GetInstance()->targetCamera_ = targetCamera; }

	void AxisIndicator::SetVisible(bool isVisible) { GetInstance()->isVisible_ = isVisible; }

	void AxisIndicator::Initialize() {
		// モデル初期化・設定
		// --------------------------------------------
		model_.reset(Model::CreateFromOBJ(kModelName));

		// 専用カメラ初期化・設定
		// --------------------------------------------
		camera_.Initialize();
		camera_.fovAngleY = 0.45f;
		camera_.aspectRatio = kViewPortWidth / kViewPortHeight;
		camera_.nearZ = 0.1f;
		camera_.farZ = 100.0f;

		// ワールドトランスフォーム初期化・設定
		// --------------------------------------------
		worldTransform_.Initialize();
		worldTransform_.scale_ = { 0.5f, 0.5f, 0.5f };
		worldTransform_.rotation_ = { 0.0f, 0.0f, 0.0f };
		worldTransform_.translation_ = { 0.0f, 0.0f, 0.0f };

		// 初期化完了
		isInitialized_ = true;
	}

	void AxisIndicator::Update() {
		if (!isInitialized_ || targetCamera_ == nullptr) {
			return;
		}

		// ターゲットカメラの回転を同期
		Matrix4x4 viewRotate = targetCamera_->matView;
		viewRotate.m[3][0] = 0.0f;
		viewRotate.m[3][1] = 0.0f;
		viewRotate.m[3][2] = 0.0f;

		// 回転更新
		// --------------------------------------------
		camera_.matView = viewRotate * MakeTranslateMatrix({ 0.0f, 0.0f, kCameraDistance });
		camera_.UpdateProjectionMatrix();
		camera_.TransferConstBuffer();

		worldTransform_.UpdateMatrix(camera_);
	}

	void AxisIndicator::Draw() {
		if (!isInitialized_ || !isVisible_ || model_ == nullptr || targetCamera_ == nullptr) {
			return;
		}

		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
		assert(commandList);

		// --------------------------------------------
		// ミニミニビューポート・ミニミニシザー（右上）を作成。
		// --------------------------------------------
		D3D12_VIEWPORT axisViewport{};
		axisViewport.TopLeftX = kViewPortTopLeftX;
		axisViewport.TopLeftY = kViewPortTopLeftY;
		axisViewport.Width = kViewPortWidth;
		axisViewport.Height = kViewPortHeight;
		axisViewport.MinDepth = 0.0f;
		axisViewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &axisViewport);

		D3D12_RECT axisScissorRect{};
		axisScissorRect.left = static_cast<LONG>(kViewPortTopLeftX);
		axisScissorRect.top = static_cast<LONG>(kViewPortTopLeftY);
		axisScissorRect.right = static_cast<LONG>(kViewPortTopLeftX + kViewPortWidth);
		axisScissorRect.bottom = static_cast<LONG>(kViewPortTopLeftY + kViewPortHeight);
		commandList->RSSetScissorRects(1, &axisScissorRect);

		// 深度残ってると、たまに見えんくなるからここだけ深度リセット
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetGameRenderDsvHandle();
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &axisScissorRect);

		// モデル描画
		model_->Draw(worldTransform_, camera_);

		// --------------------------------------------
		// 軸の描画が終わったんで、メインにビューポート・シザーをお返しします。
		// --------------------------------------------
		D3D12_VIEWPORT defaultViewport{};
		defaultViewport.TopLeftX = 0.0f;
		defaultViewport.TopLeftY = 0.0f;
		defaultViewport.Width = static_cast<float>(WinApp::kWindowWidth);
		defaultViewport.Height = static_cast<float>(WinApp::kWindowHeight);
		defaultViewport.MinDepth = 0.0f;
		defaultViewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &defaultViewport);

		D3D12_RECT defaultScissorRect{};
		defaultScissorRect.left = 0;
		defaultScissorRect.top = 0;
		defaultScissorRect.right = WinApp::kWindowWidth;
		defaultScissorRect.bottom = WinApp::kWindowHeight;
		commandList->RSSetScissorRects(1, &defaultScissorRect);
	}

} // namespace KujataEngine
