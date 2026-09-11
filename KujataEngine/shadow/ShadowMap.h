#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"

namespace KujataEngine{

/// <summary>
/// Object3d.PS.hlsl の cbuffer gShadow と完全に一致させること(b5)。
/// </summary>
struct ShadowConstants {
	float lightViewProjection[16] = {};
	float shadowBias = 0.0015f;   // 深度比較のオフセット。大きいと影が浮き、小さいと縞が出る
	float shadowTexelSize = 0.0f; // 1.0 / kResolution。PCFのサンプル間隔
	float padding[2] = {};
};

class ShadowMap {
public:

	// 解像度。2048^2 * 4B = 16MB
	static constexpr uint32_t kResolution = 2048;

	static KUJATA_API ShadowMap* GetInstance();
	
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// ライトの向きと影の範囲からビュープロジェクション行列を作る。
	/// </summary>
	/// <param name="lightDirection">ライトの向き</param>
	/// <param name="focusPosition"></param>
	/// <param name="halfExtent"></param>
	/// <param name="depthRange"></param>
	void UpdateLightMatrix(const Vector3& lightDirection, const Vector3& focusPosition, float halfExtent, float depthRange);

	/// <summary>
	/// 深度書き込み開始
	/// </summary>
	void BeginWrite();

	/// <summary>
	/// 深度書き込み終了
	/// </summary>
	void EndWrite();

	const Matrix4x4& GetLightViewProjection() const { return lightViewProjection_; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU() const { return srvHandleGPU_; }

	/// Object3dのPixelShaderへ渡すb5の実体。
	ID3D12Resource* GetConstBuffer() const { return constantResource_.Get(); }

	/// 深度比較のオフセット。縞(アクネ)が出るなら上げ、影が浮くなら下げる。
	float GetShadowBias() const { return shadowBias_; }
	void SetShadowBias(float bias);

	bool IsInitialized() const { return initialized_; }

private:
	ShadowMap() = default;
	~ShadowMap() = default;
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	/// 現在のlightViewProjection_/bias/texelSizeを定数バッファへ書き戻す。
	void UpdateConstantBuffer();

	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU_{};
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_{};
	Matrix4x4 lightViewProjection_{};

	// PixelShaderへ渡す定数バッファ(マップしっぱなし)。
	Microsoft::WRL::ComPtr<ID3D12Resource> constantResource_;
	ShadowConstants* constantMap_ = nullptr;
	float shadowBias_ = 0.0015f;

	bool initialized_ = false;

};


}
