#pragma once

#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <functional>
#include <wrl.h>

#include "../base/DirectXCommon.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "PostEffectPipeline.h"
#include "VolumeProfile.h"

namespace KujataEngine {

class Camera;

/// <summary>
/// ポストプロセス実行クラス。
/// ビュー毎(Scene/Game)のブルーム中間RT・トーンマップ済みResolve RTを管理し、
/// EndSceneRender/EndGameRender直後にHDRシーンRTへブルーム+トーンマップを適用する。
/// ImGui::Imageで表示するのはここのResolve RT(LDR)になる。
/// </summary>
class PostProcess {
public:
	/// <summary>
	/// トーンマップ後のLDR RTへ重ねて描くコールバック(Screen Space UIなど)。
	/// 引数は描画先RTのピクセルサイズ。呼ばれた時点でRTVは設定済み・RENDER_TARGET状態。
	/// </summary>
	using OverlayDrawFunc = std::function<void(float targetWidth, float targetHeight)>;

	static KUJATA_API PostProcess* GetInstance();

	/// <summary>
	/// 初期化。PostEffectPipeline::Initializeの後に呼ぶこと。
	/// </summary>
	void Initialize();

	/// <summary>
	/// sourceに描かれたHDRシーンへフォグ+ブルーム+トーンマップを適用し、ビュー専用のResolve RT(LDR)へ出力する。
	/// EndSceneRender/EndGameRenderの直後に呼ぶこと(sourceはPIXEL_SHADER_RESOURCE状態)。
	/// 終了時に描画先はバックバッファへ戻る。cameraはフォグの深度復元用(nullptrならフォグをスキップ)。
	/// drawOverlayを渡すと、トーンマップ直後のResolve RTへ重ねて描ける(ポストの影響を受けないUI用)。
	/// </summary>
	void Render(uint32_t viewIndex, const RenderTexture& source, const Camera* camera = nullptr, const OverlayDrawFunc& drawOverlay = nullptr);

	/// <summary>
	/// エディタ無しビルド用: フォグ+ブルーム適用後、最終トーンマップをSwapChainバックバッファへ直接出力する。
	/// PreDraw後(バックバッファがRENDER_TARGET状態)に呼ぶこと。
	/// drawOverlayを渡すと、トーンマップ直後のバックバッファへ重ねて描ける(ポストの影響を受けないUI用)。
	/// </summary>
	void RenderToBackBuffer(const RenderTexture& source, const Camera* camera = nullptr, const OverlayDrawFunc& drawOverlay = nullptr);

	/// <summary>
	/// ImGui::Imageで表示する、ポスト適用済みRT(LDR)のSRVハンドル。
	/// まだ一度もRenderされていないビューは元のシーンRTのSRVを返す(起動直後のフォールバック)。
	/// </summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetDisplaySrvHandle(uint32_t viewIndex) const;

	/// <summary>
	/// このフレームで適用するポストエフェクト設定を差し替える。
	/// シーン上のVolumeをVolumeStackが解決した結果を毎フレーム渡す想定で、
	/// PostProcess自身は設定の権威を持たない(シーンごとに違う値にできるのはこのため)。
	/// </summary>
	KUJATA_API void SetActiveProfile(const VolumeProfileData& profile) { activeProfile_ = profile; }

	/// <summary>現在適用中の設定。Volumeが1つも無いシーンでは既定値(ポスト無し相当)になる。</summary>
	KUJATA_API const VolumeProfileData& GetActiveProfile() const { return activeProfile_; }

	/// <summary>
	/// 画面フェード(シーン遷移演出用)。amount=0で無効、1で完全にfadeColorになる。
	/// </summary>
	KUJATA_API void SetFade(const Vector3& color, float amount);
	KUJATA_API float GetFadeAmount() const { return fadeAmount_; }

private:
	PostProcess() = default;
	~PostProcess() = default;
	PostProcess(const PostProcess&) = delete;
	PostProcess& operator=(const PostProcess&) = delete;

	// ブルームmip/Resolveなど、ポスト用の小さなRT1枚分。
	struct PostTarget {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
		int32_t width = 0;
		int32_t height = 0;
		bool handlesAllocated = false; // ディスクリプタスロットはリサイズ時も使い回す
	};

	static const uint32_t kMaxBloomMipCount = 4; // 1/2〜1/16

	// 1ビュー(Scene/Game)分のポスト用RT一式。
	struct ViewTargets {
		PostTarget bloomMips[kMaxBloomMipCount];
		uint32_t activeMipCount = 0;
		PostTarget fogScratch; // フォグ適用後のHDRシーン(トーンマップの入力になる)
		PostTarget resolve; // トーンマップ後のLDR出力(ImGui::Imageが表示する)
		int32_t sourceWidth = 0; // 生成時のsourceサイズ(リサイズ検知用)
		int32_t sourceHeight = 0;
	};

	// sourceサイズが変わっていたら中間RT一式を作り直す(ハンドルは使い回す)。
	void EnsureTargets(ViewTargets& targets, int32_t width, int32_t height);
	// PostTargetのリソースとViewを(再)生成する。クリアしない運用なのでClearValueはnullptr。
	void RecreateTarget(PostTarget& target, int32_t width, int32_t height, DXGI_FORMAT resourceFormat, DXGI_FORMAT viewFormat);
	// ブルームチェーン(BrightPass→Down→Up)を実行し、結果をbloomMips[0]へ残す。
	void RenderBloomChain(ViewTargets& targets, const RenderTexture& source);
	// 全パス共通のルート定数を現在の設定から作る。
	PostConstants MakeConstants() const;
	// リソースステート遷移(RENDER_TARGET⇔PIXEL_SHADER_RESOURCE)。
	void TransitionTarget(PostTarget& target, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	// 指定サイズのViewport/Scissorを積む。
	void SetViewportScissor(int32_t width, int32_t height);
	// フォグパスを実行する(source+深度→fogScratch)。カメラの逆VP行列で深度からワールド座標を復元する。
	void DrawFog(const RenderTexture& source, ViewTargets& targets, const Camera* camera);
	// トーンマップパスを積む(出力先RTV/サイズは呼び出し側で設定済みであること)。
	// sceneSrvはHDRシーンの入力(フォグ適用済みならfogScratch、そうでなければsource)。
	void DrawTonemap(D3D12_GPU_DESCRIPTOR_HANDLE sceneSrv, const ViewTargets& targets, bool bloomActive);

private:
	// 毎フレームVolumeStackから受け取る、解決済みのポストエフェクト設定。
	VolumeProfileData activeProfile_;
	ViewTargets viewTargets_[DirectXCommon::kRenderViewCount];

	// 画面フェード(Volumeとは独立した実行時の演出状態)。
	Vector3 fadeColor_ = {0.0f, 0.0f, 0.0f};
	float fadeAmount_ = 0.0f;
};

} // namespace KujataEngine
