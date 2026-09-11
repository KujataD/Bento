#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "../3d/GraphicsPipeline.h"
#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"

namespace KujataEngine {

class Camera;
class WorldTransform;

/// <summary>
/// world空間に置く1枚のテクスチャ付き矩形(Sprite方式のプリミティブ)。
///
/// スクリーン空間UI用の <see cref="UIQuad"/> と対になり、シェーダー(UI.VS/UI.PS)を共有する。
/// 違いはWVPに何を渡すかと深度の扱いだけ:
///   - UIQuad     : ピクセル座標 + 正射影。深度OFFで常に手前。
///   - SpriteQuad : ローカル頂点 + Transform + カメラのVP。深度テストはするが**書き込まない**。
///
/// Plane等の3Dメッシュとの決定的な違いは、深度を書かない(=スプライト同士がZで争わない)ことと、
/// ライティング・マテリアルアセットを一切通さないこと。前後関係はSorting Orderの描画順で決まる。
/// </summary>
class SpriteQuad {
public:
	KUJATA_API void Initialize();

	/// <summary>
	/// ローカル矩形をサイズ(world単位)とピボット(0..1)から作る。
	/// ピボットが原点になるので、pivot=(0.5,0.5)で中心基準、(0,0)で左上基準。
	/// </summary>
	KUJATA_API void SetSize(const Vector2& size, const Vector2& pivot);

	/// <summary>UVの左右/上下反転(Unity 2DのFlip X/Y相当)。</summary>
	KUJATA_API void SetFlip(bool flipX, bool flipY);

	KUJATA_API void SetColor(const Vector4& color);

	/// <summary>UVトランスフォーム。合成順はSprite/UI/Materialと同じ Scale→RotateZ→Translate。</summary>
	KUJATA_API void SetUVTransform(const Vector2& offset, const Vector2& scale, float rotation);

	KUJATA_API void SetTexture(uint32_t textureIndex) { textureIndex_ = textureIndex; }
	KUJATA_API void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

	/// <summary>
	/// kSprite2Dパイプラインでworld空間に矩形を描画する。ライティングは通さない(色 x テクスチャのみ)。
	/// Sprite2DRendererのパスから、Sorting Order順に呼ばれる想定。
	/// </summary>
	KUJATA_API void Draw(const WorldTransform& worldTransform, const Camera& camera);

	bool IsInitialized() const { return initialized_; }

private:
	/// <summary>UI.PSのMaterialと同じレイアウト(b0)。3D用のMaterialDataとは別物。</summary>
	struct SpriteQuadMaterial {
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
		Matrix4x4 uvTransform;
		Vector4 flags = {0.0f, 0.0f, 0.0f, 0.0f}; // x: SDFフォント用。スプライトでは常に0。
	};

	/// <summary>現在のsize/pivot/flip設定から頂点(位置とUV)を作り直す。</summary>
	KUJATA_API void UpdateVertices();

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexData* vertexMap_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	SpriteQuadMaterial* materialMap_ = nullptr;

	Vector2 size_ = {1.0f, 1.0f};
	Vector2 pivot_ = {0.5f, 0.5f};
	bool flipX_ = false;
	bool flipY_ = false;

	uint32_t textureIndex_ = 0;
	BlendMode blendMode_ = BlendMode::kNormal;
	bool initialized_ = false;
};

} // namespace KujataEngine
