#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "../base/DirectXCommon.h"

namespace KujataEngine {

// ポストプロセスのパス種別。
// 明部抽出(BrightPass)は廃止: 閾値処理はObject3d.PSがマテリアル別にエミッションRTへ書く時点で行う。
enum class PostEffectType {
	kFog,             // 距離+高さフォグ、SpotLightによる霧の色付け(HDRシーンへ適用、ブルームより前)
	kBloomDownsample, // 13タップダウンサンプル(初段はエミッションRT→1/2解像度)
	kBloomUpsample,   // 3x3テントアップサンプル(加算ブレンドで1段上へ累積)
	kTonemap,         // ブルーム合成+露出+トーンマップ(出力: LDR sRGB)
	kCountOfPostEffectType,
};

// 全ポストパス共有のルート定数。
// HLSL側 Data/shader/PostEffect.hlsli の PostConstants と完全に一致させること。
struct PostConstants {
	float texelSize[2] = {0.0f, 0.0f}; // 入力テクスチャの1テクセルサイズ
	float bloomIntensity = 0.0f;       // ブルーム合成の全体強度(0で寄与なし)
	float exposure = 1.0f;             // 露出
	int32_t tonemapType = 0;           // 0=None / 1=Reinhard / 2=ACES
	float padding0[3] = {};
	float fade[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // rgb=フェード色 / w=適用量
	float saturation = 1.0f;                  // 彩度(1=そのまま)
	float contrast = 1.0f;                    // コントラスト(1=そのまま)
	float vignetteIntensity = 0.0f;           // ビネット強度(0で無効)
	float vignetteSmoothness = 0.4f;          // ビネットの滑らかさ
	float colorFilter[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算カラーフィルタ
};

// フォグパス専用のルート定数。
// HLSL側 Data/shader/Fog.PS.hlsl の FogConstants と完全に一致させること。
struct FogConstants {
	float invViewProjection[16]; // ビュープロジェクション逆行列(深度→ワールド座標復元用)
	float cameraWorldPosition[3];
	float enabled = 0.0f;  // 0/1(HLSL側の分岐を単純にするためfloatで持つ)
	float fogColor[3] = {1.0f, 1.0f, 1.0f};
	float density = 0.0f;       // 距離ベースの指数フォグ密度係数
	float heightFalloff = 0.0f; // 高さによる密度減衰
	float heightBase = 0.0f;    // この高さを基準に上ほど薄くなる
	float startDistance = 0.0f; // このカメラ距離までフォグを効かせない
	float maxDistance = 1.0f;   // レイマーチの最大距離(遠景クランプ)
	float spotScatter = 0.0f;   // スポットライトが霧を照らす強さ(演出用係数)
	float padding[3] = {};
};

/// <summary>
/// ポストプロセス用パイプライン管理クラス。
/// フルスクリーン三角形VS + 各ポストパスのRootSignature/PSOを生成・保持する。
/// GraphicsPipelineとは別管理(BlendMode全組合せ・入力レイアウト・深度が不要なため)。
/// </summary>
class PostEffectPipeline {
public:
	// ブルーム中間RTのフォーマット。入力になるエミッションRTと同一フォーマットで扱う。
	static constexpr DXGI_FORMAT kBloomFormat = DirectXCommon::kSceneEmissionFormat;

	// ルートパラメータ番号。全パス共通のRootSignatureを使う。
	static const uint32_t kRootParamTexture0 = 0; // t0 主入力
	static const uint32_t kRootParamTexture1 = 1; // t1 副入力(Tonemapのブルーム/Fogの深度)
	static const uint32_t kRootParamConstants = 2; // b0 ルート定数(PostConstants)
	static const uint32_t kRootParamFogConstants = 3; // b1 ルート定数(FogConstants。Fogパスのみ使用)
	static const uint32_t kRootParamSpotLightCBV = 4; // b4 SpotLightData(CBV。Fogパスのみ使用)

	static PostEffectPipeline* GetInstance();

	/// <summary>
	/// RootSignatureと全ポストパスのPSOを生成する。
	/// DXCを再利用するため、GraphicsPipeline::Initializeの後に呼ぶこと。
	/// </summary>
	void Initialize();

	/// <summary>
	/// 指定パスのRootSignature+PSO+ルート定数をコマンドリストへ積む。
	/// 呼び出し側でOMSetRenderTargets/Viewport/SRVテーブルを設定した後、DrawInstanced(3,1,0,0)する。
	/// </summary>
	void SetCommandList(PostEffectType type, const PostConstants& constants);

	/// <summary>
	/// Fogパス専用のルート定数(b1)とSpotLightのCBV(b4)を積む。SetCommandList(kFog, ...)の後に呼ぶこと。
	/// </summary>
	void SetFogConstants(const FogConstants& constants, D3D12_GPU_VIRTUAL_ADDRESS spotLightCbvAddress);

	bool IsInitialized() const { return initialized_; }

private:
	PostEffectPipeline() = default;
	~PostEffectPipeline() = default;
	PostEffectPipeline(const PostEffectPipeline&) = delete;
	PostEffectPipeline& operator=(const PostEffectPipeline&) = delete;

	void CreateRootSignature();
	void CreatePipelineStates();

private:
	bool initialized_ = false;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_[static_cast<int32_t>(PostEffectType::kCountOfPostEffectType)];
};

} // namespace KujataEngine
