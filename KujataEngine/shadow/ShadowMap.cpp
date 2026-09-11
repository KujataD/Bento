#include "ShadowMap.h"
#include "../base/DirectXCommon.h"
#include "../math/MathUtil.h"

#include <cassert>
#include <cmath>
#include <cstring>

namespace KujataEngine {

ShadowMap* ShadowMap::GetInstance() {
	static ShadowMap instance;
	return &instance;
}

void ShadowMap::Initialize() {

	// DirectXCommon取得
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12Device* device = dxCommon->GetDevice();

	// ============================================================
	// シャドウマップ用テクスチャ作成
	// ============================================================

	// GPU専用メモリ(Default Heap)を使用
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 深度テクスチャの設定
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = kResolution;               // シャドウマップ解像度
	desc.Height = kResolution;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;

	// DSV(D32_FLOAT)とSRV(R32_FLOAT)の両方で利用するため
	// TYPELESSフォーマットでリソースを作成する
	desc.Format = DXGI_FORMAT_R32_TYPELESS;

	desc.SampleDesc.Count = 1;

	// 深度バッファとして利用可能にする
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 深度クリア値
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;

	// 深度テクスチャ生成
	// 初期状態はPixelShaderResourceにしておく
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&depthResource_));
	assert(SUCCEEDED(hr));

	// ============================================================
	// DSV(DepthStencilView)作成
	// シャドウ描画時の出力先
	// ============================================================

	uint32_t dsvIndex = dxCommon->AllocateDsvIndex();

	dsvHandle_ = dxCommon->GetDsvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();

	dsvHandle_.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeDSV()) * dsvIndex;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// 深度テクスチャにDSVを作成
	device->CreateDepthStencilView(
		depthResource_.Get(),
		&dsvDesc,
		dsvHandle_);

	// ============================================================
	// SRV(ShaderResourceView)作成
	// 本描画時にシャドウマップを参照するため
	// ============================================================

	uint32_t srvIndex = dxCommon->AllocateSrvIndex();

	// CPUハンドル
	srvHandleCPU_ = dxCommon->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvHandleCPU_.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;

	// GPUハンドル
	srvHandleGPU_ = dxCommon->GetSrvDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	srvHandleGPU_.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

	// TYPELESS(R32_TYPELESS)を
	// シェーダ側ではR32_FLOATとして参照する
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;

	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	// 深度テクスチャにSRVを作成
	// ピクセルシェーダからシャドウ判定に利用する
	device->CreateShaderResourceView(
		depthResource_.Get(),
		&srvDesc,
		srvHandleCPU_);

	// ============================================================
	// 定数バッファ(b5)作成
	// 本描画のPixelShaderへライト行列とバイアスを渡す
	// ============================================================

	// ルートCBVは256バイト境界を要求するので切り上げておく。
	constexpr size_t kConstantBufferSize = (sizeof(ShadowConstants) + 0xff) & ~size_t(0xff);
	constantResource_ = dxCommon->CreateBufferResource(kConstantBufferSize);
	hr = constantResource_->Map(0, nullptr, reinterpret_cast<void**>(&constantMap_));
	assert(SUCCEEDED(hr));

	lightViewProjection_ = MakeIdentity();
	UpdateConstantBuffer();

	// 初期化完了
	initialized_ = true;
}

void ShadowMap::UpdateConstantBuffer() {
	if (!constantMap_) {
		return;
	}
	std::memcpy(constantMap_->lightViewProjection, lightViewProjection_.m, sizeof(constantMap_->lightViewProjection));
	constantMap_->shadowBias = shadowBias_;
	constantMap_->shadowTexelSize = 1.0f / static_cast<float>(kResolution);
}

void ShadowMap::SetShadowBias(float bias) {
	shadowBias_ = bias;
	UpdateConstantBuffer();
}

void ShadowMap::BeginWrite() {
	if (!initialized_) {
		return;
	}
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// SRVとして読める状態から、深度を書ける状態へ遷移する。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// カラー出力は無し。深度だけを描画先にする。
	commandList->OMSetRenderTargets(0, nullptr, false, &dsvHandle_);
	commandList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Viewport/Scissorはシャドウマップの解像度(画面サイズとは無関係)。
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kResolution);
	viewport.Height = static_cast<float>(kResolution);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.right = static_cast<LONG>(kResolution);
	scissorRect.bottom = static_cast<LONG>(kResolution);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void ShadowMap::EndWrite() {
	if (!initialized_) {
		return;
	}
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// 本描画のPixelShaderから読めるSRV状態へ戻す。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthResource_.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);
}

void ShadowMap::UpdateLightMatrix(const Vector3& lightDirection, const Vector3& focusPosition, float halfExtent, float depthRange) {
	Vector3 direction = Normalize(lightDirection);

	// 方向ベクトルからEuler角へ(LH・+Z前方)。上下逆に出たらpitchの符号を反転する。
	float yaw = std::atan2(direction.x, direction.z);
	float pitch = std::asin(-direction.y);

	// ライトを範囲の外まで引いて、focusPositionを見下ろす位置に置く。
	Vector3 lightPosition = focusPosition - direction * (depthRange * 0.5f);

	Matrix4x4 lightWorld = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, { pitch, yaw, 0.0f }, lightPosition);
	Matrix4x4 view = Inverse(lightWorld);
	// 平行光源なので正射影。nearを0にすると精度が落ちるので少し前から始める。
	Matrix4x4 projection = MakeOrthographicMatrix(-halfExtent, halfExtent, halfExtent, -halfExtent, 0.1f, depthRange);

	lightViewProjection_ = view * projection;
	UpdateConstantBuffer();
}


} // namespace KujataEngine