#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

#include "../3d/Camera.h"
#include "../3d/GraphicsPipeline.h"
#include "../3d/WorldTransform.h"
#include "../runtime/KujataApi.h"
#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

#include "../../externals/DirectXTex/DirectXTex.h"

namespace KujataEngine {

enum FillMode { kFillModeSolid, kFillModeWireframe };

/// <summary>
/// 3Dモデル
/// </summary>
class KUJATA_API Model {
public:
	Model() = default;
	~Model() = default;

	/// <summary>
	/// OBJファイルからモデルを生成する(省略版)
	/// </summary>
	static Model* CreateFromOBJ(const std::string& objname, ShaderModel shaderModel = ShaderModel::kNone);
	static Model* CreateFromGlTF(const std::string& objname, ShaderModel shaderModel = ShaderModel::kNone);
	static Model* TryCreateFromFile(const std::string& filePath, ShaderModel shaderModel = ShaderModel::kNone);

	static Model* CreateSphere(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone, uint32_t subdivision = 16);

	static Model* CreateCube(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone);

	/// <summary>
	/// カプセル(cylinder + 両端半球)のテンプレモデルを生成する。
	/// heightは両端の半球を含む全長で、cylinder部分の長さは height - 2*radius。
	/// </summary>
	static Model* CreateCapsule(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone, float radius = 0.5f, float height = 2.0f, uint32_t subdivision = 16);

	/// <summary>
	/// シャドウマップへ深度だけ書く。lightViewProjectionはShadowMapが作るライト視点の行列。
	/// 呼ぶ前に DirectXCommon::SetRenderViewIndex(kShadowViewIndex) と
	/// ShadowMap::BeginWrite() を済ませておくこと。
	/// </summary>
	void DrawShadow(const WorldTransform& worldTransform, const Matrix4x4& lightViewProjection);

	static Model* CreatePlane(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone);

	/// <summary>
	/// XZ平面に寝た円環(ドーナツ状の板)を作ります。**地面に置く輪の標準形**で、
	/// 衝撃波・魔法陣・攻撃予告円のように「広がる輪」「地面の円」を出したいときに使います。
	/// 板を1枚張ってシェーダーで中心を捨てる作り方と違い、**捨てるピクセルが無い**のが利点。
	///
	/// 外半径は1(スケールで拡大する前提)。innerRatioが内半径の割合(0.55なら太さ45%の輪)。
	/// UVは u=円周方向(0〜1) / **v=帯の内→外(0〜1)**。vを使うと帯の内外へ向けて色や濃さを変えられる。
	/// 法線は+Y(上向き)。裏からも見せたい場合は ModelRendererComponent の Double Sided を使う。
	/// </summary>
	/// <summary>
	/// 毎フレーム頂点を書き換えられる空のメッシュを作ります(トレイル・リボン・波紋など、形が動くもの用)。
	/// maxVertices分のバッファを最初に確保し、以後は UpdateDynamicVertices で
	/// **中身と使用頂点数だけ**を差し替える(GPUリソースの作り直しをしない)。
	/// 三角形リストなので、頂点数は3の倍数で渡すこと。
	/// </summary>
	static Model* CreateDynamic(uint32_t maxVertices, const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone);

	/// <summary>
	/// CreateDynamicで作ったメッシュの頂点を差し替えます。確保数を超えた分は切り捨てます。
	/// 空(0頂点)にすれば描画されません。
	/// </summary>
	void UpdateDynamicVertices(const std::vector<VertexData>& vertices);

	static Model* CreateRing(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone, uint32_t subdivision = 48,
	    float innerRatio = 0.55f);

	static Model* CreateTriangle(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone);

	static Model* CreateTetrahedron(const std::string& textureFilePath, ShaderModel shaderModel = ShaderModel::kNone);

	/// <summary>
	/// 描画前処理（全モデル共通・フレームに1回）
	/// RootSignature / PSO / Viewport / ScissorRect / PrimitiveTopology をセットする
	/// main.cpp のループ内「描画用設定」に対応
	/// </summary>
	static void PreDraw();

	/// <summary>
	/// 描画後処理（将来の拡張のために用意）
	/// </summary>
	static void PostDraw();

	/// <summary>
	/// 描画（PreDraw の後に呼ぶ）
	/// </summary>
	void Draw(const WorldTransform& worldTransform, const Camera& camera, FillMode fillMode = kFillModeSolid);

	// --- set ---
	// 色/テクスチャ/シェーダーは全サブメッシュへ一括適用する(方式B: Material AssL一括上書き)。
	void SetColor(const Vector4& color) {
		for (SubMesh& subMesh : subMeshes_) {
			if (subMesh.materialMap) {
				subMesh.materialMap->color = color;
			}
		}
	}
	void SetBlendMode(BlendMode mode) { blendMode_ = mode; }
	/// 両面描画(背面カリングなし)。バリア球など内側からも見せたいものに使う。
	void SetDoubleSided(bool doubleSided) { doubleSided_ = doubleSided; }
	bool IsDoubleSided() const { return doubleSided_; }
	/// 深度バッファへ書き込むか。半透明/加算はfalseにする(深度テストは行うので不透明物には隠される)。
	void SetDepthWrite(bool depthWrite) { depthWrite_ = depthWrite; }
	bool IsDepthWrite() const { return depthWrite_; }
	void SetTexture(uint32_t textureIndex) {
		for (SubMesh& subMesh : subMeshes_) {
			subMesh.textureIndex = textureIndex;
		}
	}
	/// <summary>
	/// エミッションマップ(自己発光の分布)を差し替える。SetEmissiveの色/強度へ乗算されるので、
	/// 黒い箇所は光らず白い箇所だけが光る。未設定(白1x1)ならマップ無しと同じ挙動になる。
	/// </summary>
	void SetEmissiveTexture(uint32_t textureIndex) {
		for (SubMesh& subMesh : subMeshes_) {
			subMesh.emissiveTextureIndex = textureIndex;
		}
	}
	// シェーダー方式を切り替える(MaterialData.enableLightingがShaderModel番号を兼ねる。0=Unlit)。
	void SetShaderModel(ShaderModel model) {
		for (SubMesh& subMesh : subMeshes_) {
			if (subMesh.materialMap) {
				subMesh.materialMap->enableLighting = static_cast<int32_t>(model);
			}
		}
	}
	// UVトランスフォームを全サブメッシュへ一括適用する(合成順はSprite/UIと同じ Scale→RotateZ→Translate)。
	void SetUVTransform(const Vector2& offset, const Vector2& scale, float rotation);

	// サブメッシュ単位のUVトランスフォーム(MultiMeshのパーツ別UV調整用。indexが範囲外なら無視)。
	void SetSubMeshUVTransform(size_t index, const Vector2& offset, const Vector2& scale, float rotation);
	// エミッション(自己発光)を全サブメッシュへ一括適用する。enabled=falseなら発光しない。
	// 強度>1でHDR輝度になりブルームが乗る。
	void SetEmissive(const Vector3& color, float intensity, bool enabled) {
		for (SubMesh& subMesh : subMeshes_) {
			if (subMesh.materialMap) {
				subMesh.materialMap->emissiveColor = color;
				subMesh.materialMap->emissiveIntensity = intensity;
				subMesh.materialMap->emissiveEnabled = enabled ? 1 : 0;
			}
		}
	}
	// 露出光(ブルーム)のマテリアル別パラメータを全サブメッシュへ一括適用する。
	// エミッションRTへの書き込み(=滲み方)だけを制御し、発光色そのものには影響しない。
	void SetEmissiveBloom(float bloomIntensity, float bloomThreshold, float bloomSoftKnee) {
		for (SubMesh& subMesh : subMeshes_) {
			if (subMesh.materialMap) {
				subMesh.materialMap->bloomIntensity = bloomIntensity;
				subMesh.materialMap->bloomThreshold = bloomThreshold;
				subMesh.materialMap->bloomSoftKnee = bloomSoftKnee;
			}
		}
	}

	/// <summary>
	/// ワールド座標でのタイリング(トライプラナー)を全サブメッシュへ設定する。
	/// 値は1ワールドユニットあたりの繰り返し数。0でメッシュのUV貼りに戻る。
	/// </summary>
	void SetTriplanarScale(float scale) {
		for (SubMesh& subMesh : subMeshes_) {
			if (subMesh.materialMap) {
				subMesh.materialMap->triplanarScale = scale;
			}
		}
	}

	// --- get ---

	// raycast/preview用: 全サブメッシュを統合した頂点列(AABB計算に使う)。
	const std::vector<VertexData>& GetVertices() const { return mergedVertices_; }

	// サブメッシュ数。>1 ならMultiMesh(埋め込みマテリアルをパーツ別に持つ)。
	size_t GetSubMeshCount() const { return subMeshes_.size(); }

	const Matrix4x4& GetRootLocalMatrix() const { return rootLocalMatrix_; }

private:
	Model(const Model&) = delete;
	Model& operator=(const Model&) = delete;

	// 1サブメッシュ分のGPUリソース(頂点バッファ + マテリアルCBuffer + テクスチャ)。
	struct SubMesh {
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		uint32_t vertexCount = 0;
		// --- 動的メッシュ用 ---
		// 確保済みの頂点数。毎フレーム作り直さず、ここまでの範囲でvertexCountだけを変える。
		uint32_t vertexCapacity = 0;
		// Uploadヒープを張りっぱなしにしたポインタ(動的メッシュのみ)。毎フレームMapし直さないため。
		VertexData* vertexMap = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
		MaterialData* materialMap = nullptr;
		uint32_t textureIndex = 0;
		// エミッションマップ(t2)。既定0はTextureManagerの未使用枠なので、Draw時に白へフォールバックする。
		uint32_t emissiveTextureIndex = 0;
	};

	std::vector<SubMesh> subMeshes_;
	// 全サブメッシュを統合した頂点(GetVertices用)。
	std::vector<VertexData> mergedVertices_;
	Matrix4x4 rootLocalMatrix_ = MakeIdentity();

	// 頂点とマテリアルから1サブメッシュ分のGPUリソースを生成し、subMeshes_へ追加する。
	void AddSubMesh(const std::vector<VertexData>& vertices, const MaterialData& material);

	// ModelDataのサブメッシュ(空なら統合vertices/materialでフォールバック)から
	// サブメッシュ群を構築する。各マテリアルにshaderModelとテクスチャindexを解決して設定する。
	void BuildSubMeshes(ModelData& modelData, ShaderModel shaderModel);

	BlendMode blendMode_ = BlendMode::kNormal;
	bool doubleSided_ = false;
	bool depthWrite_ = true;
};

} // namespace KujataEngine
