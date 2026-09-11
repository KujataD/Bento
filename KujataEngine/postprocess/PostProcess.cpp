#include "PostProcess.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "../3d/Camera.h"
#include "../3d/SpotLight.h"

namespace KujataEngine {

PostProcess* PostProcess::GetInstance() {
	static PostProcess instance;
	return &instance;
}

void PostProcess::Initialize() {
	// 現状はRT遅延生成(初回Render時)なのでここでは何もしない。
	// 将来、固定解像度RTの事前生成が必要になったらここで行う。
}

void PostProcess::SetFade(const Vector3& color, float amount) {
	fadeColor_ = color;
	fadeAmount_ = std::clamp(amount, 0.0f, 1.0f);
}

D3D12_GPU_DESCRIPTOR_HANDLE PostProcess::GetDisplaySrvHandle(uint32_t viewIndex) const {
	assert(viewIndex < DirectXCommon::kRenderViewCount);
	const ViewTargets& targets = viewTargets_[viewIndex];
	if (targets.resolve.resource) {
		return targets.resolve.srvHandleGPU;
	}
	// まだ一度もポストパスが走っていない(起動直後など)場合は元のシーンRTを表示する。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	return (viewIndex == DirectXCommon::kGameViewIndex) ? dxCommon->GetGameRenderSrvHandle() : dxCommon->GetSceneRenderSrvHandle();
}

void PostProcess::EnsureTargets(ViewTargets& targets, int32_t width, int32_t height) {
	if (targets.sourceWidth == width && targets.sourceHeight == height) {
		return;
	}
	// 毎フレームPostDrawでGPU完了を待つ設計のため、前フレームまでに旧リソースを参照するコマンドは残っていない。
	// そのままComPtr再代入で解放してよい(追加のWaitForGpu不要)。
	targets.sourceWidth = width;
	targets.sourceHeight = height;

	// Resolve(トーンマップ後LDR)はsourceと同解像度。
	// リソースはUNORM、View/出力は_SRGB(リニア値をHWでsRGBエンコードし、ImGuiのSRVでデコードする既存RTと同じ流儀)。
	RecreateTarget(targets.resolve, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DirectXCommon::kResolveColorFormat);

	// フォグ適用後のHDRシーン(トーンマップの入力)。sourceと同解像度・同フォーマット。
	RecreateTarget(targets.fogScratch, width, height, DirectXCommon::kSceneColorFormat, DirectXCommon::kSceneColorFormat);

	// ブルーム縮小チェーン(1/2〜1/16)。最小辺が8px未満になる段は作らない。
	targets.activeMipCount = 0;
	int32_t mipWidth = width;
	int32_t mipHeight = height;
	for (uint32_t i = 0; i < kMaxBloomMipCount; ++i) {
		mipWidth /= 2;
		mipHeight /= 2;
		if (mipWidth < 8 || mipHeight < 8) {
			break;
		}
		RecreateTarget(targets.bloomMips[i], mipWidth, mipHeight, PostEffectPipeline::kBloomFormat, PostEffectPipeline::kBloomFormat);
		targets.activeMipCount = i + 1;
	}
}

void PostProcess::RecreateTarget(PostTarget& target, int32_t width, int32_t height, DXGI_FORMAT resourceFormat, DXGI_FORMAT viewFormat) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12Device* device = dxCommon->GetDevice();

	// ディスクリプタスロットは初回のみ確保し、リサイズ時は使い回す(RenderTextureと同じ流儀)。
	if (!target.handlesAllocated) {
		uint32_t rtvIndex = dxCommon->AllocateRtvIndex();
		target.rtvHandle = dxCommon->GetRtvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
		target.rtvHandle.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeRTV()) * rtvIndex;

		uint32_t srvIndex = dxCommon->AllocateSrvIndex();
		target.srvHandleCPU = dxCommon->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
		target.srvHandleCPU.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;
		target.srvHandleGPU = dxCommon->GetSrvDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
		target.srvHandleGPU.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;
		target.handlesAllocated = true;
	}

	target.width = width;
	target.height = height;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC colorDesc{};
	colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	colorDesc.Width = static_cast<UINT64>(width);
	colorDesc.Height = static_cast<UINT>(height);
	colorDesc.DepthOrArraySize = 1;
	colorDesc.MipLevels = 1;
	colorDesc.Format = resourceFormat;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// 毎フレーム全ピクセルを上書きするためClearは行わない。ClearValueをnullptrにして
	// 「クリア値とクリア色の不一致」デバッグレイヤー警告を根本的に避ける。
	target.resource.Reset();
	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties,
	    D3D12_HEAP_FLAG_NONE,
	    &colorDesc,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	    nullptr,
	    IID_PPV_ARGS(&target.resource));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = viewFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(target.resource.Get(), &rtvDesc, target.rtvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = viewFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(target.resource.Get(), &srvDesc, target.srvHandleCPU);
}

PostConstants PostProcess::MakeConstants() const {
	PostConstants constants;
	constants.bloomIntensity = 0.0f; // Tonemapで必要なときだけ上書きする
	constants.exposure = activeProfile_.tonemap.exposure;
	constants.tonemapType = activeProfile_.tonemap.tonemapper;
	constants.fade[0] = fadeColor_.x;
	constants.fade[1] = fadeColor_.y;
	constants.fade[2] = fadeColor_.z;
	constants.fade[3] = fadeAmount_;
	constants.saturation = activeProfile_.colorGrading.saturation;
	constants.contrast = activeProfile_.colorGrading.contrast;
	constants.vignetteIntensity = activeProfile_.vignette.intensity;
	constants.vignetteSmoothness = activeProfile_.vignette.smoothness;
	constants.colorFilter[0] = activeProfile_.colorGrading.colorFilter.x;
	constants.colorFilter[1] = activeProfile_.colorGrading.colorFilter.y;
	constants.colorFilter[2] = activeProfile_.colorGrading.colorFilter.z;
	constants.colorFilter[3] = activeProfile_.colorGrading.colorFilter.w;
	return constants;
}

void PostProcess::TransitionTarget(PostTarget& target, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = target.resource.Get();
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);
}

void PostProcess::SetViewportScissor(int32_t width, int32_t height) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.right = width;
	scissorRect.bottom = height;
	commandList->RSSetScissorRects(1, &scissorRect);
}

void PostProcess::RenderBloomChain(ViewTargets& targets, const RenderTexture& source) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	PostEffectPipeline* pipeline = PostEffectPipeline::GetInstance();

	// --- 初段ダウンサンプル: エミッションRT(フル解像度) → mips[0](1/2解像度) ---
	// 入力はEmissionチェック付きマテリアルの発光だけが書かれた専用RT。
	// 閾値処理はマテリアル別にObject3d.PS側で済んでいるため、ここは縮小のみ。
	{
		PostTarget& mip0 = targets.bloomMips[0];
		TransitionTarget(mip0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->OMSetRenderTargets(1, &mip0.rtvHandle, false, nullptr);
		SetViewportScissor(mip0.width, mip0.height);

		PostConstants constants = MakeConstants();
		constants.texelSize[0] = 1.0f / static_cast<float>(source.width);
		constants.texelSize[1] = 1.0f / static_cast<float>(source.height);
		pipeline->SetCommandList(PostEffectType::kBloomDownsample, constants);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture0, source.emissionSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture1, source.emissionSrvHandleGPU);
		commandList->DrawInstanced(3, 1, 0, 0);
		TransitionTarget(mip0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// --- ダウンサンプル連鎖: mips[i-1] → mips[i] ---
	for (uint32_t i = 1; i < targets.activeMipCount; ++i) {
		PostTarget& src = targets.bloomMips[i - 1];
		PostTarget& dst = targets.bloomMips[i];
		TransitionTarget(dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->OMSetRenderTargets(1, &dst.rtvHandle, false, nullptr);
		SetViewportScissor(dst.width, dst.height);

		PostConstants constants = MakeConstants();
		constants.texelSize[0] = 1.0f / static_cast<float>(src.width);
		constants.texelSize[1] = 1.0f / static_cast<float>(src.height);
		pipeline->SetCommandList(PostEffectType::kBloomDownsample, constants);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture0, src.srvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture1, src.srvHandleGPU);
		commandList->DrawInstanced(3, 1, 0, 0);
		TransitionTarget(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// --- アップサンプル連鎖: mips[i] を mips[i-1] へテント拡大+加算(裾の広いブルームを累積) ---
	for (uint32_t i = targets.activeMipCount - 1; i >= 1; --i) {
		PostTarget& src = targets.bloomMips[i];
		PostTarget& dst = targets.bloomMips[i - 1];
		TransitionTarget(dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->OMSetRenderTargets(1, &dst.rtvHandle, false, nullptr);
		SetViewportScissor(dst.width, dst.height);

		PostConstants constants = MakeConstants();
		constants.texelSize[0] = 1.0f / static_cast<float>(src.width);
		constants.texelSize[1] = 1.0f / static_cast<float>(src.height);
		pipeline->SetCommandList(PostEffectType::kBloomUpsample, constants);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture0, src.srvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture1, src.srvHandleGPU);
		commandList->DrawInstanced(3, 1, 0, 0);
		TransitionTarget(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void PostProcess::DrawFog(const RenderTexture& source, ViewTargets& targets, const Camera* camera) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	PostEffectPipeline* pipeline = PostEffectPipeline::GetInstance();

	PostTarget& dst = targets.fogScratch;
	TransitionTarget(dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->OMSetRenderTargets(1, &dst.rtvHandle, false, nullptr);
	SetViewportScissor(dst.width, dst.height);

	// フォグ定数を組み立てる。逆VP行列は深度→ワールド座標復元用。
	FogConstants fogConstants;
	Matrix4x4 invViewProjection = Inverse(camera->matView * camera->matProjection);
	std::memcpy(fogConstants.invViewProjection, invViewProjection.m, sizeof(fogConstants.invViewProjection));
	fogConstants.cameraWorldPosition[0] = camera->translation_.x;
	fogConstants.cameraWorldPosition[1] = camera->translation_.y;
	fogConstants.cameraWorldPosition[2] = camera->translation_.z;
	fogConstants.enabled = 1.0f;
	fogConstants.fogColor[0] = activeProfile_.fog.color.x;
	fogConstants.fogColor[1] = activeProfile_.fog.color.y;
	fogConstants.fogColor[2] = activeProfile_.fog.color.z;
	fogConstants.density = activeProfile_.fog.density;
	fogConstants.heightFalloff = activeProfile_.fog.heightFalloff;
	fogConstants.heightBase = activeProfile_.fog.heightBase;
	fogConstants.startDistance = activeProfile_.fog.startDistance;
	fogConstants.maxDistance = activeProfile_.fog.maxDistance;
	fogConstants.spotScatter = activeProfile_.fog.spotScatter;

	pipeline->SetCommandList(PostEffectType::kFog, MakeConstants());
	pipeline->SetFogConstants(fogConstants, SpotLight::GetInstance()->GetResource()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture0, source.srvHandleGPU);
	commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture1, source.depthSrvHandleGPU);
	commandList->DrawInstanced(3, 1, 0, 0);
	TransitionTarget(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void PostProcess::DrawTonemap(D3D12_GPU_DESCRIPTOR_HANDLE sceneSrv, const ViewTargets& targets, bool bloomActive) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	PostEffectPipeline* pipeline = PostEffectPipeline::GetInstance();

	PostConstants constants = MakeConstants();
	constants.bloomIntensity = bloomActive ? activeProfile_.bloom.intensity : 0.0f;
	pipeline->SetCommandList(PostEffectType::kTonemap, constants);
	commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture0, sceneSrv);
	// ブルーム無効時はt1を読まない(intensity=0)が、未バインドを避けるためsceneSrvを入れておく。
	D3D12_GPU_DESCRIPTOR_HANDLE bloomSrv = bloomActive ? targets.bloomMips[0].srvHandleGPU : sceneSrv;
	commandList->SetGraphicsRootDescriptorTable(PostEffectPipeline::kRootParamTexture1, bloomSrv);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void PostProcess::Render(uint32_t viewIndex, const RenderTexture& source, const Camera* camera, const OverlayDrawFunc& drawOverlay) {
	assert(viewIndex < DirectXCommon::kRenderViewCount);
	if (!PostEffectPipeline::GetInstance()->IsInitialized()) {
		return;
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	ViewTargets& targets = viewTargets_[viewIndex];
	EnsureTargets(targets, source.width, source.height);

	const bool bloomActive = activeProfile_.bloom.enabled && targets.activeMipCount > 0;
	if (bloomActive) {
		RenderBloomChain(targets, source);
	}

	// --- フォグ: HDRシーン+深度 → fogScratch(HDR) ---
	const bool fogActive = activeProfile_.fog.enabled && camera != nullptr;
	if (fogActive) {
		DrawFog(source, targets, camera);
	}

	// --- トーンマップ: HDRシーン(フォグ適用済みならfogScratch)+ブルーム → Resolve RT(LDR) ---
	TransitionTarget(targets.resolve, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->OMSetRenderTargets(1, &targets.resolve.rtvHandle, false, nullptr);
	SetViewportScissor(targets.resolve.width, targets.resolve.height);
	DrawTonemap(fogActive ? targets.fogScratch.srvHandleGPU : source.srvHandleGPU, targets, bloomActive);

	// --- ポスト後のオーバーレイ(Screen Space UI): トーンマップ済みLDRへ直接描く ---
	// ここで描いたものはフォグ/ブルーム/露出/トーンマップの影響を受けない。
	if (drawOverlay) {
		drawOverlay(static_cast<float>(targets.resolve.width), static_cast<float>(targets.resolve.height));
		// オーバーレイ側がビューポート等を変えるため、以降のパスに備えて戻しておく。
		SetViewportScissor(targets.resolve.width, targets.resolve.height);
	}

	TransitionTarget(targets.resolve, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// この後のImGui描画のため、描画先をバックバッファへ戻す(EndRenderTextureと同じ規約)。
	dxCommon->SetBackBufferRenderTarget();
}

void PostProcess::RenderToBackBuffer(const RenderTexture& source, const Camera* camera, const OverlayDrawFunc& drawOverlay) {
	if (!PostEffectPipeline::GetInstance()->IsInitialized()) {
		return;
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	// エディタ無しビルドはGameビューのターゲットを使う(中間RTはブルーム/フォグ用のみ使用)。
	ViewTargets& targets = viewTargets_[DirectXCommon::kGameViewIndex];
	EnsureTargets(targets, source.width, source.height);

	const bool bloomActive = activeProfile_.bloom.enabled && targets.activeMipCount > 0;
	if (bloomActive) {
		RenderBloomChain(targets, source);
	}

	const bool fogActive = activeProfile_.fog.enabled && camera != nullptr;
	if (fogActive) {
		DrawFog(source, targets, camera);
	}

	// バックバッファはPreDrawでRENDER_TARGET状態になっている。DSVは使わないので外して積む。
	D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv = dxCommon->GetBackBufferRtvHandle();
	commandList->OMSetRenderTargets(1, &backBufferRtv, false, nullptr);
	SetViewportScissor(dxCommon->GetBackBufferWidth(), dxCommon->GetBackBufferHeight());
	DrawTonemap(fogActive ? targets.fogScratch.srvHandleGPU : source.srvHandleGPU, targets, bloomActive);

	// --- ポスト後のオーバーレイ(Screen Space UI): トーンマップ済みバックバッファへ直接描く ---
	if (drawOverlay) {
		drawOverlay(static_cast<float>(dxCommon->GetBackBufferWidth()), static_cast<float>(dxCommon->GetBackBufferHeight()));
	}

	// 後続処理(ImGui等)のために描画先とビューポートを通常状態へ戻す。
	dxCommon->SetBackBufferRenderTarget();
	SetViewportScissor(dxCommon->GetBackBufferWidth(), dxCommon->GetBackBufferHeight());
}

} // namespace KujataEngine
