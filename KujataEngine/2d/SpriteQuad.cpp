#include "SpriteQuad.h"

#include "../3d/Camera.h"
#include "../3d/WorldTransform.h"
#include "../base/DirectXCommon.h"
#include "../base/TextureManager.h"
#include "../math/MathUtil.h"

namespace KujataEngine {

void SpriteQuad::Initialize() {
	if (initialized_) {
		return;
	}
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	// 頂点(4)。Mapしたままにしてサイズ/ピボット/Flip変更を随時反映する。
	vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * 4);
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap_));

	// インデックス(6)。頂点順・UVマッピングはModel::CreatePlaneに合わせ、
	// 既存のPlaneプリミティブとテクスチャの向きが一致するようにする(kSprite2Dは両面表示)。
	indexResource_ = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
	uint32_t* indexMap = nullptr;
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexMap));
	indexMap[0] = 0;
	indexMap[1] = 1;
	indexMap[2] = 2;
	indexMap[3] = 2;
	indexMap[4] = 1;
	indexMap[5] = 3;
	indexResource_->Unmap(0, nullptr);

	// マテリアルCBV(UI.PSのMaterialレイアウト)。Map先はUploadヒープの生メモリなので明示的に初期化する。
	materialResource_ = dxCommon->CreateBufferResource((sizeof(SpriteQuadMaterial) + 0xFF) & ~0xFF);
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap_));
	materialMap_->color = {1.0f, 1.0f, 1.0f, 1.0f};
	materialMap_->uvTransform = MakeIdentity();
	materialMap_->flags = {0.0f, 0.0f, 0.0f, 0.0f};

	textureIndex_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
	initialized_ = true;
	UpdateVertices();
}

void SpriteQuad::SetSize(const Vector2& size, const Vector2& pivot) {
	size_ = size;
	pivot_ = pivot;
	UpdateVertices();
}

void SpriteQuad::SetFlip(bool flipX, bool flipY) {
	flipX_ = flipX;
	flipY_ = flipY;
	UpdateVertices();
}

void SpriteQuad::UpdateVertices() {
	if (!vertexMap_) {
		return;
	}

	// Model::CreatePlaneと同じUVマッピング(u=0側が+X、v=0側が+Y)。
	// ピボットが原点に来るよう、u=0/v=0側の端をpivot分だけずらす。
	const float xAtU0 = pivot_.x * size_.x;
	const float xAtU1 = -(1.0f - pivot_.x) * size_.x;
	const float yAtV0 = pivot_.y * size_.y;
	const float yAtV1 = -(1.0f - pivot_.y) * size_.y;

	const float u0 = flipX_ ? 1.0f : 0.0f;
	const float u1 = flipX_ ? 0.0f : 1.0f;
	const float v0 = flipY_ ? 1.0f : 0.0f;
	const float v1 = flipY_ ? 0.0f : 1.0f;

	vertexMap_[0].position = {xAtU0, yAtV0, 0.0f, 1.0f};
	vertexMap_[0].texcoord = {u0, v0};
	vertexMap_[1].position = {xAtU1, yAtV0, 0.0f, 1.0f};
	vertexMap_[1].texcoord = {u1, v0};
	vertexMap_[2].position = {xAtU0, yAtV1, 0.0f, 1.0f};
	vertexMap_[2].texcoord = {u0, v1};
	vertexMap_[3].position = {xAtU1, yAtV1, 0.0f, 1.0f};
	vertexMap_[3].texcoord = {u1, v1};

	for (int i = 0; i < 4; ++i) {
		vertexMap_[i].normal = {0.0f, 0.0f, 1.0f};
	}
}

void SpriteQuad::SetColor(const Vector4& color) {
	if (materialMap_) {
		materialMap_->color = color;
	}
}

void SpriteQuad::SetUVTransform(const Vector2& offset, const Vector2& scale, float rotation) {
	if (!materialMap_) {
		return;
	}
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix({scale.x, scale.y, 1.0f});
	uvTransformMatrix = uvTransformMatrix * MakeRotateZMatrix(rotation);
	uvTransformMatrix = uvTransformMatrix * MakeTranslateMatrix({offset.x, offset.y, 0.0f});
	materialMap_->uvTransform = uvTransformMatrix;
}

void SpriteQuad::Draw(const WorldTransform& worldTransform, const Camera& camera) {
	if (!initialized_) {
		return;
	}

	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// WVPをこのビュー用の定数バッファへ転送する(Scene/Game両ビュー対応)。
	// UI.VSはb0の先頭float4x4(=WVP)だけを読むので、WorldTransformの定数バッファをそのまま使える。
	worldTransform.TransferMatrix(camera);

	GraphicsPipeline::GetInstance()->SetCommandList(PipelineType::kSprite2D, blendMode_);

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	// RootParameterはUI系と同一構成(b0=Material(PS), b0=Transform(VS), t0=Texture)。ライトは使わない。
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, worldTransform.GetConstBuffer()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandle(textureIndex_));

	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

} // namespace KujataEngine
