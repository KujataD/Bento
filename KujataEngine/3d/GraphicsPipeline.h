#pragma once

#include <array>
#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <vector>
#include <wrl.h>

#pragma comment(lib, "dxcompiler.lib")

#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

namespace KujataEngine {

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct MaterialData {
	// --- ここからCB転送部。HLSL側 Object3d.PS.hlsl の Material と完全一致させること ---
	// HLSLのパッキングでは shininess(offset96) の直後に float3 が同じ16Bレジスタへ詰められるため、
	// emissiveColor は offset100、emissiveIntensity は offset112 となりC++の自然配置と一致する。
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	int32_t enableLighting = 0;
	float pad[3] = {};
	Matrix4x4 uvTransform;
	float shininess = 40.0f;
	// エミッション(自己発光)。emissiveEnabled!=0のときだけ加算される(マテリアルのEmissionチェック)。
	// 強度>1でHDR輝度を出力しブルームを誘発する。
	Vector3 emissiveColor = {1.0f, 1.0f, 1.0f};
	float emissiveIntensity = 1.0f;
	int32_t emissiveEnabled = 0;
	// 露出光(ブルーム)のマテリアル別パラメータ。エミッション専用RT(SV_TARGET1)への書き込みを制御する。
	float bloomIntensity = 1.0f; // 滲みの強さ(エミッションRTへ書く値のスケール。0で滲まない)
	float bloomThreshold = 0.0f; // この輝度以上のエミッションだけが滲む(0=全て滲む)
	float bloomSoftKnee = 0.5f;  // 閾値の柔らかさ(0=ハード)
	// ワールド座標でのタイリング(トライプラナー)。0でメッシュのUVをそのまま使う。
	// 値は「1ワールドユニットあたりの繰り返し数」なので、4ユニットで1周させたいなら0.25。
	// **プリミティブのUVは面ごとに0..1固定**で、Transformで引き伸ばすと面ごとに違う伸び方をする。
	// これを使うと面の実寸に応じて敷き詰められ、どのオブジェクトでも密度が揃う。
	float triplanarScale = 0.0f;
	// --- ここまでCB転送部 ---
	std::string textureFilePath;
	uint32_t textureIndex;
};

struct Node {
	Matrix4x4 localMatrix = {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
	std::string name;
	std::vector<Node> children;
};

/// <summary>
/// 1サブメッシュ分の頂点とマテリアル。MultiMesh(複数メッシュ/マテリアル)のパーツ単位。
/// </summary>
struct MeshData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

struct ModelData {
	// 後方互換: 全メッシュを統合した頂点と代表マテリアル(ParticleModel/InstancingModelが使用)。
	std::vector<VertexData> vertices;
	MaterialData material;
	// サブメッシュ別データ。Modelのマルチメッシュ描画で使用。
	std::vector<MeshData> meshes;
	Node rootNode;
};

enum class BlendMode {
	kNone,      // ブレンドなし
	kNormal,    // αブレンド	: Src * SrcA + Dest * (1 - SrcA)
	kAdd,       // 加算		: Src * SrcA + Dest * 1
	kSubtract,  // 減算		: Dest * 1 - Src * SrcA
	kMultiply,  // 乗算		: Src * 0 + Dest * Src
	kScreen,    // スクリーン	: Src * (1 - Dest) + Dest * 1
	kExclusion, // 除外		: (1 - Dest) * Src + (1 - Src) * Dest
	kPremultipliedAlpha, // 乗算済みα: Src * 1 + Dest * (1 - SrcA)。テキスト等のフチが暗くならない。

	kCountOfBlendMode, //!< ブレンドモード数。指定はしない
};

enum class PipelineType {
	kObject3d,
	kParticle,
	kInstancingObject3d,
	kObject3dWireframe,
	kObject3dDoubleSided, // kObject3dと同じだが背面カリングなし(バリア球・リボン等の薄いものを内側からも見せる)
	// 深度書き込みOFF版。半透明/加算は深度を書くと「後から描く背後の物」を隠し、
	// 自分の裏面とも喧嘩するため、透ける材質はこちらを使う(深度テストは行う)。
	kObject3dNoDepthWrite,
	kObject3dDoubleSidedNoDepthWrite,
	kLine,
	kUI,      // スクリーン空間UI(深度OFF・アルファブレンド・ポスト適用後のLDR RTへ出力)
	kSprite2D, // world空間2Dスプライト(深度テストON・深度書き込みOFF・両面・アルファブレンド)
	kCountOfPipeLineType,
};

enum class ShaderModel {
	kNone,                 // シェーダーなし
	kLambert,              // ランバート
	kHalfLambert,          // ハーフランバート
	kPhongReflection,      // フォンリフレクション
	kBlingPhongReflection, // ブリンフォンリフレクション
	// 衝撃波の輪。UVの中心からの距離で中空のリングを描く。光らせない土煙の表現で、
	// **オブジェクトを拡大すればリングがそのまま外へ広がる**(進捗パラメータの受け渡しが要らない)。
	// リングの外と内は完全に透明にしてdiscardさせるため、半透明が深度を汚さない。
	kShockwaveRing,
	// 六角形20枚+五角形12枚(切頂二十面体)のセルが浮かぶバリア。
	// UVではなく法線方向から手続き的にセルを求めるため、球のUVの極の潰れが出ない。
	// セルの境界だけが光り、面の内側は薄く透ける。色と不透明度はマテリアルのBase Colorで決める。
	kBarrier,
	// トレイル(TrailRendererComponentの帯)。UVのu(先頭0→末尾1)に沿って薄くなり、
	// 幅方向(v)の縁もぼかす。末尾が自然に消えるので、帯の切れ目が線として見えない。
	kTrail,
};

/// <summary>
/// グラフィックスパイプライン管理クラス
/// RootSignature・シェーダーコンパイル・PSOの生成を担当する
/// </summary>
class GraphicsPipeline {
public:
public:
	friend class Model;
	friend class InstancingModel;
	static GraphicsPipeline* GetInstance();

	/// <summary>
	/// 初期化
	/// RootSignature・シェーダー・PSOを生成する
	/// </summary>
	void Initialize();

	/// <summary>
	/// 描画前コマンドのセット
	/// RootSignature・PSO・DescriptorHeap・Viewport・ScissorRect をコマンドリストに積む
	/// </summary>
	void SetCommandList(PipelineType pipelineType, BlendMode blendMode);

	void Finalize();

	/// <summary>
	/// HLSLを実行時コンパイルする。PostEffectPipeline等、他のPSO生成クラスからも共用する
	/// (DXCインスタンスの重複生成を避けるためここへ集約)。
	/// </summary>
	IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile);

private:
	GraphicsPipeline() = default;
	~GraphicsPipeline() = default;
	GraphicsPipeline(const GraphicsPipeline&) = delete;
	GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

	void InitializeDXC();

	void CreateObject3dRootSignature();

	void CreateInstancingRootSignature();

	void CreateLineRootSignature();

	void CreateObject3dPipelineStateObject();

	void CreateInstancingPipelineStateObject();

	void CreateLinePipelineStateObject();

	/// <summary>
	/// UI.VS/UI.PS を使うパイプライン(kUI / kSprite2D)のRootSignatureを作る。
	/// 構成は共通(b0=Material(PS), b0=Transform(VS), t0=Texture)なのでtypeで使い分ける。
	/// </summary>
	void CreateUIStyleRootSignature(PipelineType pipelineType);

	/// <summary>
	/// UI.VS/UI.PS を使うPSOを作る。
	/// depthTestEnabled=false: スクリーン空間UI(常に手前)。true: world空間2D(3Dに遮蔽されるが深度は書かない)。
	/// ldrTarget=true: ポスト適用後のLDR RT(RTV1枚・深度なし)へ出力する。
	/// false: HDRシーンRT(カラー+エミッションのMRT・深度あり)へ出力する。
	/// </summary>
	void CreateUIStylePipelineStateObject(PipelineType pipelineType, bool depthTestEnabled, bool ldrTarget);

private:
	// DXCコンパイラ関連
	IDxcUtils* dxcUtils_ = nullptr;
	IDxcCompiler3* dxcCompiler_ = nullptr;
	IDxcIncludeHandler* includeHandler_ = nullptr;

	// パイプライン
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_[static_cast<int32_t>(PipelineType::kCountOfPipeLineType)];
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_[static_cast<int32_t>(PipelineType::kCountOfPipeLineType)][static_cast<int32_t>(BlendMode::kCountOfBlendMode)];
};

} // namespace KujataEngine
