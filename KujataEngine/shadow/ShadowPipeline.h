#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace KujataEngine {

/// <summary>
/// シャドウマップ描画専用のRootSignature/PSO。
/// カラー出力もマテリアルもテクスチャも要らず、深度だけを書く軽いパスなので
/// GraphicsPipeline(BlendMode全組合せ・ライト・テクスチャを持つ)とは分けて管理する。
/// </summary>
class ShadowPipeline {
public:
	// ルートパラメータ番号。
	static const uint32_t kRootParamTransform = 0; // b0(VS) TransformationMatrix(WVPはライト視点)

	static ShadowPipeline* GetInstance();

	/// <summary>
	/// RootSignatureとPSOを生成する。
	/// DXCを再利用するため、GraphicsPipeline::Initializeの後に呼ぶこと。
	/// </summary>
	void Initialize();

	/// <summary>
	/// RootSignatureとPSOをコマンドリストへ積む。
	/// 呼び出し側でOMSetRenderTargets/Viewport(=ShadowMap::BeginWrite)を済ませてから使う。
	/// </summary>
	void SetCommandList();

	bool IsInitialized() const { return initialized_; }

private:
	ShadowPipeline() = default;
	~ShadowPipeline() = default;
	ShadowPipeline(const ShadowPipeline&) = delete;
	ShadowPipeline& operator=(const ShadowPipeline&) = delete;

	void CreateRootSignature();
	void CreatePipelineState();

private:
	bool initialized_ = false;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};

} // namespace KujataEngine
