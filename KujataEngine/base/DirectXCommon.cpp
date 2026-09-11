#include "DirectXCommon.h"
#include "FrameProfiler.h"
#include "Logger.h"
#include "StringUtil.h"
#include <cassert>
#include <format>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace KujataEngine {

DirectXCommon* DirectXCommon::GetInstance() {
	static DirectXCommon instance;
	return &instance;
}

void DirectXCommon::Initialize(WinApp* winApp, Vector4 color , int32_t backBufferWidth, int32_t backBufferHeight, bool enableDebugLayer) {
	assert(winApp);

	clearColor_[0] = color.x;
	clearColor_[1] = color.y;
	clearColor_[2] = color.z;
	clearColor_[3] = color.w;

	winApp_ = winApp;
	backBufferWidth_ = backBufferWidth;
	backBufferHeight_ = backBufferHeight;

	InitializeDXGIDevice(enableDebugLayer);
	InitializeDescriptorSize();
	InitializeCommand();
	CreateSwapChain();
	CreateFinalRenderTargets();
	CreateDepthBuffer();
	CreateGameRenderTarget();
	CreateFence();

	initialized_ = true;
}

void DirectXCommon::InitializeDXGIDevice(bool enableDebugLayer) {
#pragma region デバッグレイヤー
#ifdef _DEBUG
	if (enableDebugLayer) {
		Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			// デバッグレイヤーを有効化する
			debugController->EnableDebugLayer();
			// さらにGPU側でもチェックを行うようにする
			debugController->SetEnableGPUBasedValidation(TRUE);
		}
	}
#endif
#pragma endregion

#pragma region DXGIFactoryの生成
	// HRESULTはWindows系のエラーコードであり、関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));
#pragma endregion

#pragma region 使用するアダプタ（GPU）を決定する

	// 使用するアダプタ用の変数。最初にnullptrを入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	// 良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
		// アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できないのは一大事
		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			// 採用したアダプタの情報をログに出力。wstringの方なので注意
			Logger::Log(StringUtil::ToString(std::format(L"Use Adapter: {}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

#pragma endregion

#pragma region D3D12Deviceの生成
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
	const char* featureLevelStrings[] = {"12.2", "12.1", "12.0"};
	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// 生成できたのでログ出力を行ってループを抜ける
			Logger::Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	assert(device_ != nullptr);
	Logger::Log("Complete create D3D12Device!!!\n");

#pragma endregion
#pragma region DX12のエラーチェック
#ifdef _DEBUG
	if (enableDebugLayer) {
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			// ヤバイエラー時に止まる
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			// エラー時に止まる
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

			// 抑制するメッセージのID
			D3D12_MESSAGE_ID denyIds[] = {// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			                              // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			                              D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE}; // 抑制するレベル
			// 抑制するレベル
			D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
			D3D12_INFO_QUEUE_FILTER filter{};
			filter.DenyList.NumIDs = _countof(denyIds);
			filter.DenyList.pIDList = denyIds;
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			// 指定したメッセージの表示を抑制する
			infoQueue->PushStorageFilter(&filter);
		}
	}
#endif
#pragma endregion
}

void DirectXCommon::InitializeCommand() {
	HRESULT hr;

	// コマンドキューを生成する
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//// コマンドアロケータを生成する
	// hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	//// コマンドアロケータの生成がうまくいかなかったので起動できない
	// assert(SUCCEEDED(hr));
	for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
		// コマンドアロケータを生成する
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators_[i]));
		// コマンドアロケータの生成がうまくいかなかったので起動できない
		assert(SUCCEEDED(hr));
	}

	// コマンドリストを生成する
	// hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeDescriptorSize() {
	descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

void DirectXCommon::CreateSwapChain() {
	// スワップチェーンを生成する
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = backBufferWidth_;                      // 画面の幅。ウィンドウのクライアント領域と同じものにしておく
	swapChainDesc.Height = backBufferHeight_;                    // 画面の高さ。ウィンドウのクライアント領域と同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;           // 色の形式
	swapChainDesc.SampleDesc.Count = 1;                          // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = kSwapChainBufferCount;           // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // モニタにうつしたら、中身を破棄

	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateFinalRenderTargets() {
	HRESULT hr;

	// hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapChainResources_[0]));
	//// うまく取得できなければ起動できない
	// assert(SUCCEEDED(hr));
	// hr = swapChain_->GetBuffer(1, IID_PPV_ARGS(&swapChainResources_[1]));
	// assert(SUCCEEDED(hr));
	for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
		hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));
	}

	// RTV用のヒープを作成する。SwapChain/Gameウィンドウ/Projectモデルプレビュー用に余裕を持って確保する。
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = kRtvDescriptorCount;
	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap_));
	assert(SUCCEEDED(hr));

	// SRV用のヒープ。GameウィンドウやProjectモデルプレビューも使うため余裕を持って確保する。
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = kSrvDescriptorCount;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap_));
	assert(SUCCEEDED(hr));

	CreateSwapChainRenderTargetViews();
}

void DirectXCommon::CreateSwapChainRenderTargetViews() {
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;      // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2dテクスチャとして書き込む

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kSwapChainBufferCount];
	// まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = rtvStartHandle;
	device_->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc, rtvHandles[0]);
	for (uint32_t i = 1; i < kSwapChainBufferCount; ++i) {
		rtvHandles[i].ptr = rtvHandles[i - 1].ptr + descriptorSizeRTV_;
		device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandles[i]);
	}
}

void DirectXCommon::CreateGameRenderTarget() {
	// Gameウィンドウ用(メインカメラ・固定解像度)の描画先を作る。
	CreateRenderTexture(gameRenderTexture_, WinApp::kWindowWidth, WinApp::kWindowHeight, kGameRenderTargetRtvIndex, kGameRenderDsvIndex);
	// Sceneウィンドウ用(デバッグカメラ)の描画先を作る。初期は同解像度。表示サイズ追従は後Phaseで対応。
	CreateRenderTexture(sceneRenderTexture_, WinApp::kWindowWidth, WinApp::kWindowHeight, kSceneRenderTargetRtvIndex, kSceneRenderDsvIndex);
}

void DirectXCommon::CreateRenderTexture(RenderTexture& target, int32_t width, int32_t height, uint32_t rtvIndex, uint32_t dsvIndex) {
	target.width = width;
	target.height = height;

	// ディスクリプタハンドルを確保する(RTV/DSVは指定スロット、SRVは一元採番)。
	// リサイズ時はこれらのハンドルを使い回してリソースとViewだけ作り直す。
	target.rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.rtvHandle.ptr += descriptorSizeRTV_ * rtvIndex;
	target.dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.dsvHandle.ptr += descriptorSizeDSV_ * dsvIndex;

	uint32_t srvIndex = AllocateSrvIndex();
	target.srvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.srvHandleCPU.ptr += descriptorSizeSRV_ * srvIndex;
	target.srvHandleGPU = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	target.srvHandleGPU.ptr += descriptorSizeSRV_ * srvIndex;

	// エミッション専用RT(MRT slot1)用のRTV/SRVスロットも確保する。
	uint32_t emissionRtvIndex = AllocateRtvIndex();
	target.emissionRtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.emissionRtvHandle.ptr += descriptorSizeRTV_ * emissionRtvIndex;

	uint32_t emissionSrvIndex = AllocateSrvIndex();
	target.emissionSrvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.emissionSrvHandleCPU.ptr += descriptorSizeSRV_ * emissionSrvIndex;
	target.emissionSrvHandleGPU = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	target.emissionSrvHandleGPU.ptr += descriptorSizeSRV_ * emissionSrvIndex;

	// 深度をポストプロセス(フォグ等)から読むためのSRVスロット。
	uint32_t depthSrvIndex = AllocateSrvIndex();
	target.depthSrvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	target.depthSrvHandleCPU.ptr += descriptorSizeSRV_ * depthSrvIndex;
	target.depthSrvHandleGPU = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	target.depthSrvHandleGPU.ptr += descriptorSizeSRV_ * depthSrvIndex;

	RecreateRenderTextureResources(target);
}

void DirectXCommon::RecreateRenderTextureResources(RenderTexture& target) {
	// target.width/height と確保済みハンドルを使い、カラー/深度リソースとViewを(再)生成する。
	// 既存リソースはComPtr再代入で解放される(呼び出し側でGPU完了を待っていること)。
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// --- カラー ---
	// HDR(1.0超の輝度)を保持するためR16G16B16A16_FLOAT。FLOATフォーマットに_SRGB版は無いため、
	// リソース/ClearValue/RTV/SRVすべて同一フォーマットで扱う(sRGB変換は最終トーンマップパスで行う)。
	D3D12_RESOURCE_DESC colorDesc{};
	colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	colorDesc.Width = target.width;
	colorDesc.Height = target.height;
	colorDesc.DepthOrArraySize = 1;
	colorDesc.MipLevels = 1;
	colorDesc.Format = kSceneColorFormat;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = kSceneColorFormat;
	clearValue.Color[0] = clearColor_[0];
	clearValue.Color[1] = clearColor_[1];
	clearValue.Color[2] = clearColor_[2];
	clearValue.Color[3] = clearColor_[3];

	target.colorResource.Reset();
	HRESULT hr = device_->CreateCommittedResource(
	    &heapProperties,
	    D3D12_HEAP_FLAG_NONE,
	    &colorDesc,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	    &clearValue,
	    IID_PPV_ARGS(&target.colorResource));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = kSceneColorFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device_->CreateRenderTargetView(target.colorResource.Get(), &rtvDesc, target.rtvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = kSceneColorFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device_->CreateShaderResourceView(target.colorResource.Get(), &srvDesc, target.srvHandleCPU);

	// --- エミッション(MRT slot1) ---
	// emissionEnabledなマテリアルの発光のみが書かれる。毎フレーム黒にクリアする。
	D3D12_RESOURCE_DESC emissionDesc = colorDesc;
	emissionDesc.Format = kSceneEmissionFormat;

	D3D12_CLEAR_VALUE emissionClearValue{};
	emissionClearValue.Format = kSceneEmissionFormat;
	emissionClearValue.Color[0] = 0.0f;
	emissionClearValue.Color[1] = 0.0f;
	emissionClearValue.Color[2] = 0.0f;
	emissionClearValue.Color[3] = 1.0f;

	target.emissionResource.Reset();
	hr = device_->CreateCommittedResource(
	    &heapProperties,
	    D3D12_HEAP_FLAG_NONE,
	    &emissionDesc,
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	    &emissionClearValue,
	    IID_PPV_ARGS(&target.emissionResource));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC emissionRtvDesc{};
	emissionRtvDesc.Format = kSceneEmissionFormat;
	emissionRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device_->CreateRenderTargetView(target.emissionResource.Get(), &emissionRtvDesc, target.emissionRtvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC emissionSrvDesc{};
	emissionSrvDesc.Format = kSceneEmissionFormat;
	emissionSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	emissionSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	emissionSrvDesc.Texture2D.MipLevels = 1;
	device_->CreateShaderResourceView(target.emissionResource.Get(), &emissionSrvDesc, target.emissionSrvHandleCPU);

	// --- 深度 ---
	// フォグ等のポストプロセスからテクスチャとしても読みたいためTYPELESSで確保し、
	// DSVはD24_UNORM_S8_UINT、SRVはR24_UNORM_X8_TYPELESS(赤成分=正規化深度)として個別にViewする。
	D3D12_RESOURCE_DESC depthDesc{};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = target.width;
	depthDesc.Height = target.height;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	target.depthResource.Reset();
	hr = device_->CreateCommittedResource(
	    &heapProperties,
	    D3D12_HEAP_FLAG_NONE,
	    &depthDesc,
	    D3D12_RESOURCE_STATE_DEPTH_WRITE,
	    &depthClearValue,
	    IID_PPV_ARGS(&target.depthResource));
	assert(SUCCEEDED(hr));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(target.depthResource.Get(), &dsvDesc, target.dsvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
	depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Texture2D.MipLevels = 1;
	device_->CreateShaderResourceView(target.depthResource.Get(), &depthSrvDesc, target.depthSrvHandleCPU);
}

void DirectXCommon::ResizeSceneRenderTarget(int32_t width, int32_t height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	if (sceneRenderTexture_.width == width && sceneRenderTexture_.height == height) {
		return;
	}
	// 旧リソースがGPUで使用中の可能性があるため、解放・再生成の前にGPU完了を待つ。
	// 描画パスの外(SceneViewWindow::Draw=Update中)から呼ぶこと。
	WaitForGpu();
	sceneRenderTexture_.width = width;
	sceneRenderTexture_.height = height;
	RecreateRenderTextureResources(sceneRenderTexture_);
}

ID3D12Resource* DirectXCommon::CreateBufferResource(size_t sizeInBytes) {
	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeap
	// リソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes; // リソースのサイズ。
	// バッファの場合はこれらは1にする決まり
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// リソースを作る
	ID3D12Resource* resource = nullptr;
	HRESULT hr;
	hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

void DirectXCommon::ExecuteCommandAndWait() { // commandListをCloseし、commandQueue->ExecuteCommandListsを使いキックする
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = {commandList_.Get()};
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// 実行を待つ
	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// 実行が完了したので、allocatorとcommandListをResetして次のコマンドを積めるようにする
	// hr = commandAllocator_->Reset();
	hr = commandAllocators_[backBufferIndex_]->Reset();
	assert(SUCCEEDED(hr));
	// hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	hr = commandList_->Reset(commandAllocators_[backBufferIndex_].Get(), nullptr);
	assert(SUCCEEDED(hr));
}
void DirectXCommon::CreateDepthBuffer() {
	// DepthStencilTextureをウィンドウのサイズで作成
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = backBufferWidth_;
	resourceDesc.Height = backBufferHeight_;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;                            // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencil

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;              // 1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

	// Resourceの生成
	HRESULT hr = device_->CreateCommittedResource(
	    &heapProperties,                  // Heapの設定
	    D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。特になし
	    &resourceDesc,                    // Resourceの設定
	    D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
	    &depthClearValue,                 // Clear最適値
	    IID_PPV_ARGS(&depthStencilResource_));
	assert(SUCCEEDED(hr));

	if (!dsvDescriptorHeap_) {
		// DSV用のヒープ。通常Depth、Gameウィンドウ、Projectモデルプレビュー用Depthも確保できるようにする。
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.NumDescriptors = kDsvDescriptorCount;
		hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvDescriptorHeap_));
		assert(SUCCEEDED(hr));
	}

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;        // Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture
	// DSVHeapの先頭にDSVをつくる
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::CreateFence() {
	// 初期値0でFenceを作る
	HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_ != nullptr);
}

void DirectXCommon::PreDraw() {
	// これから書き込むバックバッファのインデックスを取得
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// TransitionBarrierの設定
	D3D12_RESOURCE_BARRIER barrier{};
	// 今回のバリアはTransition
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	// Noneにしておく
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	// バリアを張る対象のリソース。現在のバックバッファに対して行う
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	// 遷移前(現在)のResourceState
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	// 遷移後のResourceState
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// TransitionBarrier
	commandList_->ResourceBarrier(1, &barrier);

	// SRVヒープのバインドをSprite/Modelの描画側の副作用任せにすると、
	// そのフレームに描画対象が何もない場合はヒープが一度も設定されないまま
	// ImGuiのSetGraphicsRootDescriptorTableへ到達し、ドライバ側で不正アクセスになる。
	// 毎フレーム必ず一度はここでバインドしておく。
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);

	ClearRenderTarget();
	ClearDepthBuffer();
}

void DirectXCommon::ResizeBackBuffer(int32_t width, int32_t height) {
	if (!initialized_) {
		return;
	}
	if (width <= 0 || height <= 0) {
		return;
	}
	if (backBufferWidth_ == width && backBufferHeight_ == height) {
		return;
	}

	WaitForGpu();

	for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
		swapChainResources_[i].Reset();
		fenceValues_[i] = fenceValue_;
	}
	depthStencilResource_.Reset();

	HRESULT hr = swapChain_->ResizeBuffers(kSwapChainBufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	assert(SUCCEEDED(hr));

	backBufferWidth_ = width;
	backBufferHeight_ = height;
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
		hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));
	}

	CreateSwapChainRenderTargetViews();
	CreateDepthBuffer();
}

void DirectXCommon::ClearRenderTarget() {
	const uint32_t descriptorSizeRTV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += descriptorSizeRTV * backBufferIndex_;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// 描画先のRTVとDSV 
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 指定した色で画面全体をクリアする
	commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
}

void DirectXCommon::ClearDepthBuffer() {
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	// 指定した深度で画面全体をクリアする
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle() const {
	// 現在のSwapChainバックバッファに対応するRTVを取得する。
	// GameRenderの後、ImGuiをバックバッファへ描くために使う。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += descriptorSizeRTV_ * backBufferIndex_;
	return rtvHandle;
}

void DirectXCommon::SetBackBufferRenderTarget() {
	// 描画先をSwapChainのバックバッファへ戻す。
	// EndGameRender後にImGuiを通常画面へ合成するための準備。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = GetBackBufferRtvHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
}

void DirectXCommon::BeginGameRender() {
	renderViewIndex_ = kGameViewIndex;
	BeginRenderTexture(gameRenderTexture_);
}

void DirectXCommon::EndGameRender() { EndRenderTexture(gameRenderTexture_); }

void DirectXCommon::BeginRenderTexture(RenderTexture& target) {
	// RenderTextureは通常ImGuiから読まれる状態にしている。
	// ここから描画を書き込むため、カラー/エミッション両方をRENDER_TARGET状態へ遷移する。
	// 深度はフォグパスがPIXEL_SHADER_RESOURCEへ戻しているため、DEPTH_WRITEへ戻す。
	D3D12_RESOURCE_BARRIER barriers[3]{};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barriers[0].Transition.pResource = target.colorResource.Get();
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1] = barriers[0];
	barriers[1].Transition.pResource = target.emissionResource.Get();
	barriers[2] = barriers[0];
	barriers[2].Transition.pResource = target.depthResource.Get();
	barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(3, barriers);

	// 以降のDraw呼び出しがこのRenderTextureへ描かれるように、描画先を差し替える。
	// MRT: [0]=通常色 / [1]=エミッション(emissionEnabledなマテリアルのみがSV_TARGET1へ書く)。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {target.rtvHandle, target.emissionRtvHandle};
	commandList_->OMSetRenderTargets(2, rtvHandles, false, &target.dsvHandle);
	// 毎フレーム、色と深度を初期化する。エミッションは黒(発光なし)へ。
	commandList_->ClearRenderTargetView(target.rtvHandle, clearColor_, 0, nullptr);
	const float emissionClear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	commandList_->ClearRenderTargetView(target.emissionRtvHandle, emissionClear, 0, nullptr);
	commandList_->ClearDepthStencilView(target.dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Viewport/ScissorをRenderTexture全体に合わせる。ここが小さいと描画が途中で切れる。
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(target.width);
	viewport.Height = static_cast<float>(target.height);
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList_->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = target.width;
	scissorRect.bottom = target.height;
	commandList_->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::EndRenderTexture(RenderTexture& target) {
	// 描画が終わったので、ImGui::Image/ポストプロセスから読める状態へ戻す(カラー+エミッション+深度)。
	D3D12_RESOURCE_BARRIER barriers[3]{};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barriers[0].Transition.pResource = target.colorResource.Get();
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1] = barriers[0];
	barriers[1].Transition.pResource = target.emissionResource.Get();
	barriers[2] = barriers[0];
	barriers[2].Transition.pResource = target.depthResource.Get();
	barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	commandList_->ResourceBarrier(3, barriers);

	// この後のImGui描画はSwapChainバックバッファへ出したいので、描画先を戻す。
	SetBackBufferRenderTarget();
}

void DirectXCommon::PostDraw() {
	const uint32_t currentBackBufferIndex = backBufferIndex_;
	// 画面に描く処理はすべて終わり、画面に映すので、状態を遷移
	// 今回はRenderTargetからPresentにする
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	// TransitionBarrierを張る
	commandList_->ResourceBarrier(1, &barrier);

	// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = {commandList_.Get()};
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// Present〜フェンス待ちまでがGPU待ちストール(VSync含む)。計測してPerformanceWindowへ出す。
	FrameProfiler::Begin(FrameProfiler::kPresentWait);

	// GPUとOSに画面の交換を行うよう通知する。
	// vsyncEnabled_=false(PerformanceWindowで切替可)なら同期を切ってPresentし、VSync起因の落ち込みを切り分けられる。
	swapChain_->Present(vsyncEnabled_ ? 1 : 0, 0);

	// Fenceの値を更新
	fenceValue_++;
	fenceValues_[currentBackBufferIndex] = fenceValue_;
	// GPUがここまでたどり着いたら、Fenceの値を指定した値に代入するようにSignalを送る
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// 1つの定数バッファを毎フレーム上書きしているため、GPUの読み取り完了を待ってから次フレームへ進む
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// 次のフレーム用のバックバッファを取得
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// 次に使うバックバッファ用の処理がGPUで終わっていなければ待機
	if (fence_->GetCompletedValue() < fenceValues_[backBufferIndex_]) {
		// 指定したSignalにたどりついていないので、たどり着くまで待つようにイベント
		fence_->SetEventOnCompletion(fenceValues_[backBufferIndex_], fenceEvent_);
		// イベント待つ
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	FrameProfiler::End(FrameProfiler::kPresentWait);

	// 次のフレーム用のコマンドリストを準備
	//hr = commandAllocator_->Reset();
	hr = commandAllocators_[backBufferIndex_]->Reset();
	assert(SUCCEEDED(hr));
	//hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	hr = commandList_->Reset(commandAllocators_[backBufferIndex_].Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::WaitForGpu() {
	if (!commandQueue_ || !fence_) {
		return;
	}

	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}
} // namespace KujataEngine

