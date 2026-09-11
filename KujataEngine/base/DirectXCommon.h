#pragma once

#include <Windows.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>
#include <vector>
#include <wrl.h>

#include "WinApp.h"
#include <math/Vector4.h>

namespace KujataEngine {

/// <summary>
/// オフスクリーン描画先(カラー+深度+各種View+サイズ)を1単位にまとめたもの。
/// Scene/Gameなど複数ビューを別々のRenderTextureへ描くために使う。
/// </summary>
struct RenderTexture {
	Microsoft::WRL::ComPtr<ID3D12Resource> colorResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
	// 描き込み用RTV/DSV(CPUハンドル)。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
	// 深度をポストプロセス(フォグ等)からテクスチャとして読むためのSRV。
	D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandleCPU{};
	D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandleGPU{};
	// ImGui::Imageで読むためのSRV(CPU=作成用/GPU=ImGuiへ渡す)。
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU{};
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
	// エミッション専用RT(MRTのSV_TARGET1)。emissionEnabledなマテリアルの発光のみが書かれ、
	// ブルームはこちらだけを入力にする。
	Microsoft::WRL::ComPtr<ID3D12Resource> emissionResource;
	D3D12_CPU_DESCRIPTOR_HANDLE emissionRtvHandle{};
	D3D12_CPU_DESCRIPTOR_HANDLE emissionSrvHandleCPU{};
	D3D12_GPU_DESCRIPTOR_HANDLE emissionSrvHandleGPU{};
	int32_t width = 0;
	int32_t height = 0;
};

/// <summary>
/// DirectX汎用
/// </summary>
class DirectXCommon {
public:
	// シーン描画(HDR)用カラーフォーマット。1.0超の輝度(エミッション等)を保持し、
	// ポストプロセス(ブルーム/トーンマップ)の入力になる。全シーン描画PSOのRTVFormats[0]と共有する。
	static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	// エミッション専用RT(RTVFormats[1])のフォーマット。emissionEnabledなマテリアルの発光だけが
	// ここに書かれ、ブルームはこのRTのみを入力にする(明るいだけのピクセルは滲まない)。
	// 正値のみ・α不要なのでR11G11B10。ブルーム縮小チェーンとも共有する。
	static constexpr DXGI_FORMAT kSceneEmissionFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	// トーンマップ後のLDR出力用フォーマット(SwapChainバックバッファのRTVと同一系列)。
	static constexpr DXGI_FORMAT kResolveColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	static DirectXCommon* GetInstance();

	/// <param name="winApp">ウィンドウ管理クラスのポインタ</param>
	/// <param name="backBufferWidth">バックバッファ幅</param>
	/// <param name="backBufferHeight">バックバッファ高さ</param>
	/// <param name="enableDebugLayer">デバッグレイヤーの有効化</param>
	void Initialize(
	    WinApp* winApp, Vector4 color = {0.1f, 0.25f, 0.5f, 1.0f}, int32_t backBufferWidth = WinApp::kWindowWidth, int32_t backBufferHeight = WinApp::kWindowHeight, bool enableDebugLayer = false);

	void PreDraw();

	/// <summary>
	/// ウィンドウのクライアントサイズに合わせてSwapChainをリサイズする
	/// </summary>
	void ResizeBackBuffer(int32_t width, int32_t height);

	/// <summary>
	/// Gameウィンドウ用描画開始
	/// ここから後の3D/2D描画は、SwapChainではなくGame用RenderTargetへ描く
	/// </summary>
	void BeginGameRender();

	/// <summary>
	/// Gameウィンドウ用描画終了
	/// Game用RenderTargetをImGuiから読める状態へ戻し、描画先をSwapChainへ戻す
	/// </summary>
	void EndGameRender();

	/// <summary>
	/// Sceneウィンドウ用RenderTexture(デバッグカメラ描画)への描画開始/終了。
	/// </summary>
	void BeginSceneRender() {
		renderViewIndex_ = kSceneViewIndex;
		BeginRenderTexture(sceneRenderTexture_);
	}
	void EndSceneRender() { EndRenderTexture(sceneRenderTexture_); }

	// 同一フレームで同じオブジェクトを複数ビューへ描くため、ビュー毎に別々の定数バッファを使う。
	// WorldTransform等がこの番号で書き込み/バインド先のバッファを選ぶ。Scene=0/Game=1。
	static const uint32_t kRenderViewCount = 3;
	static const uint32_t kSceneViewIndex = 0;	// シーンビュー枠。デバッグカメラ視点のWVP
	static const uint32_t kGameViewIndex = 1;	// ゲームビュー枠。メインカメラ視点のWVP
	static const uint32_t kShadowViewIndex = 2;	// シャドウマップ枠。ライト視点のWVP

	void SetRenderViewIndex(uint32_t index) { renderViewIndex_ = index; }

	uint32_t GetRenderViewIndex() const { return renderViewIndex_; }

	/// <summary>
	/// 指定サイズのRenderTexture(カラー+深度+RTV/DSV/SRV)を作成する。
	/// rtvIndex/dsvIndexはそれぞれのヒープ内で使うスロット番号。
	/// </summary>
	void CreateRenderTexture(RenderTexture& target, int32_t width, int32_t height, uint32_t rtvIndex, uint32_t dsvIndex);

	/// <summary>
	/// targetを描画先にする(PIXEL_SHADER_RESOURCE→RENDER_TARGET遷移+クリア+Viewport/Scissor)。
	/// </summary>
	void BeginRenderTexture(RenderTexture& target);

	/// <summary>
	/// targetへの描画を終える(RENDER_TARGET→PIXEL_SHADER_RESOURCE遷移+描画先をバックバッファへ戻す)。
	/// </summary>
	void EndRenderTexture(RenderTexture& target);

	/// <summary>
	/// Scene用RenderTextureを指定サイズへリサイズする(サイズ変化時のみ、GPU完了を待って作り直す)。
	/// SRV等のディスクリプタスロットは維持するのでImGui::Imageのハンドルは変わらない。
	/// 描画パスの外(Update中)から呼ぶこと。
	/// </summary>
	void ResizeSceneRenderTarget(int32_t width, int32_t height);

	void PostDraw();

	void ClearRenderTarget();

	void ClearDepthBuffer();

	ID3D12Resource* CreateBufferResource(size_t sizeInBytes);

	/// <summary>
	/// CommandListにintermediateResourceをTextureResourceに転送するコマンドを積み実行完了まで待つ
	/// </summary>
	void ExecuteCommandAndWait();

	bool IsInitialized() const { return initialized_; }

	// SRV番号をDirectXCommonで一元管理する。
	uint32_t AllocateSrvIndex() {
		// kSrvDescriptorCountを超えるとSRVヒープの外側へ書き込むことになり、
		// 同じヒープを共有するImGuiフォント等のメモリを破壊して無関係な箇所で落ちる。
		assert(srvIndexCounter_ < kSrvDescriptorCount && "SRV descriptor heap overflow: increase kSrvDescriptorCount.");
		return srvIndexCounter_++;
	}

	uint32_t AllocateRtvIndex() { return rtvIndexCounter_++; }
	uint32_t AllocateDsvIndex() { return dsvIndexCounter_++; }

	// --- get ---

	ID3D12Device* GetDevice() const { return device_.Get(); }

	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }

	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
	ID3D12DescriptorHeap* GetRtvDescriptorHeap() const { return rtvDescriptorHeap_.Get(); }
	ID3D12DescriptorHeap* GetDsvDescriptorHeap() const { return dsvDescriptorHeap_.Get(); }

	// ImGui::Imageへ渡すGame用RenderTargetのGPU側SRVハンドル。
	D3D12_GPU_DESCRIPTOR_HANDLE GetGameRenderSrvHandle() const { return gameRenderTexture_.srvHandleGPU; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetGameRenderDsvHandle() const { return gameRenderTexture_.dsvHandle; }

	// ポストプロセス(PostProcess)がHDRシーンRTを入力として読むための参照。
	const RenderTexture& GetSceneRenderTexture() const { return sceneRenderTexture_; }
	const RenderTexture& GetGameRenderTexture() const { return gameRenderTexture_; }

	// ImGui::Imageへ渡すScene用RenderTextureのGPU側SRVハンドル。
	D3D12_GPU_DESCRIPTOR_HANDLE GetSceneRenderSrvHandle() const { return sceneRenderTexture_.srvHandleGPU; }
	int32_t GetSceneRenderWidth() const { return sceneRenderTexture_.width; }
	int32_t GetSceneRenderHeight() const { return sceneRenderTexture_.height; }

	uint32_t GetDescriptorSizeRTV() const { return descriptorSizeRTV_; }
	uint32_t GetDescriptorSizeSRV() const { return descriptorSizeSRV_; }
	uint32_t GetDescriptorSizeDSV() const { return descriptorSizeDSV_; }

	int32_t GetBackBufferWidth() const { return backBufferWidth_; }
	int32_t GetBackBufferHeight() const { return backBufferHeight_; }

	int32_t GetGameRenderWidth() const { return gameRenderTexture_.width; }
	int32_t GetGameRenderHeight() const { return gameRenderTexture_.height; }

	uint32_t GetSwapChainBufferCount() const { return kSwapChainBufferCount; }
	void SetBackBufferRenderTarget();

	// 現在のバックバッファに対応するRTVを取得する(エディタ無しビルドのトーンマップ直描き用)。
	D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle() const;

	// VSync(Presentの垂直同期)をランタイムで切り替える。VSync起因のFPS低下(60→30の階段)の切り分け用。
	void SetVSyncEnabled(bool enabled) { vsyncEnabled_ = enabled; }
	bool IsVSyncEnabled() const { return vsyncEnabled_; }

	// --- set ---
	void SetClearColor(Vector4 color) {
		clearColor_[0] = color.x;
		clearColor_[1] = color.y;
		clearColor_[2] = color.z;
		clearColor_[3] = color.w;
	}

private:
	DirectXCommon() = default;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	const DirectXCommon& operator=(const DirectXCommon&) = delete;

	// --- 初期化フロー ---

	/// <summary>
	/// DXGIDeviceの初期化 (
	///  デバッグレイヤー
	/// / DXGIFactoryの生成
	/// / 使用するアダプタ（GPU）の決定
	/// / D3D12Deviceの生成
	/// / DX12のエラーチェック
	/// )
	/// </summary>
	void InitializeDXGIDevice(bool enableDebugLayer);

	/// <summary>
	/// 各コマンドの初期化 (
	///  コマンドキュー
	/// / コマンドアロケータ
	/// / コマンドリスト
	/// )
	/// </summary>
	void InitializeCommand();

	void InitializeDescriptorSize();
	void CreateSwapChain();
	void CreateFinalRenderTargets();
	void CreateSwapChainRenderTargetViews();
	void CreateDepthBuffer();

	// Gameウィンドウに表示するためのOffscreen RenderTargetを作成する。
	void CreateGameRenderTarget();
	// RenderTextureのカラー/深度リソースとViewを(再)生成する(width/heightと確保済みハンドルを使う)。
	void RecreateRenderTextureResources(RenderTexture& target);
	void CreateFence();

	void WaitForGpu();

private:
	static const uint32_t kSwapChainBufferCount = 3;
	static const uint32_t kGameRenderTargetRtvIndex = kSwapChainBufferCount;
	static const uint32_t kGameRenderDsvIndex = 1;
	// Sceneビュー用RenderTexture(デバッグカメラ描画)のRTV/DSVスロット。
	static const uint32_t kSceneRenderTargetRtvIndex = kGameRenderTargetRtvIndex + 1;
	static const uint32_t kSceneRenderDsvIndex = kGameRenderDsvIndex + 1;
	static const uint32_t kRtvDescriptorCount = 256;
	// 通常テクスチャ+Project Windowのプレビュー+ImGuiフォントが同じヒープを共有するため、
	// フォルダ数やアセット数が増えても余裕を持たせておく。
	static const uint32_t kSrvDescriptorCount = 4096;
	static const uint32_t kDsvDescriptorCount = 32;
	WinApp* winApp_ = nullptr;
	bool initialized_ = false;

	// Direct3D関連
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	// Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kSwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

	// リソース関連
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[kSwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
	// Gameウィンドウへ表示するための描画先(メインカメラ・固定解像度)。
	RenderTexture gameRenderTexture_;
	// Sceneウィンドウへ表示するための描画先(デバッグカメラ・表示サイズ追従)。
	RenderTexture sceneRenderTexture_;

	// ディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

	// 同期関連
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	uint64_t fenceValues_[kSwapChainBufferCount] = {};
	HANDLE fenceEvent_ = nullptr;
	bool vsyncEnabled_ = true;

	uint32_t descriptorSizeSRV_;
	uint32_t descriptorSizeRTV_;
	uint32_t descriptorSizeDSV_;

	uint32_t backBufferIndex_ = 0;
	int32_t backBufferWidth_ = 0;
	int32_t backBufferHeight_ = 0;
	// 現在描画中のビュー番号(Scene=0/Game=1/Shadow=2)。BeginSceneRender/BeginGameRenderで切り替える。
	uint32_t renderViewIndex_ = kSceneViewIndex;

	// 画面の色
	float clearColor_[4] = {0.1f, 0.25f, 0.5f, 1.0f}; // 青っぽい色。RGBAの順

	// テクスチャのインデックス管理カウンター
	uint32_t srvIndexCounter_ = 1;
	uint32_t rtvIndexCounter_ = kSceneRenderTargetRtvIndex + 1;
	uint32_t dsvIndexCounter_ = kSceneRenderDsvIndex + 1;
};

} // namespace KujataEngine
