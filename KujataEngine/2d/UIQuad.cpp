#include "UIQuad.h"

#include "../base/DirectXCommon.h"
#include "../base/TextureManager.h"
#include "../math/MathUtil.h"
#include "UIRenderer.h"
#include <cmath>

namespace KujataEngine {

void UIQuad::Initialize() {
	if (initialized_) {
		return;
	}
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	// 頂点(4)
	vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * 4);
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap_));

	// インデックス(6): TL,TR,BL / TR,BR,BL
	indexResource_ = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
	uint32_t* indexMap = nullptr;
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexMap));
	indexMap[0] = 0;
	indexMap[1] = 1;
	indexMap[2] = 2;
	indexMap[3] = 1;
	indexMap[4] = 3;
	indexMap[5] = 2;
	indexResource_->Unmap(0, nullptr);

	// トランスフォームCBV(WVP = オルソ)
	transformResource_ = dxCommon->CreateBufferResource((sizeof(Matrix4x4) + 0xFF) & ~0xFF);
	transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformMap_));
	*transformMap_ = MakeIdentity();

	// マテリアルCBV
	materialResource_ = dxCommon->CreateBufferResource((sizeof(UIQuadMaterial) + 0xFF) & ~0xFF);
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap_));
	materialMap_->color = {1.0f, 1.0f, 1.0f, 1.0f};
	materialMap_->uvTransform = MakeIdentity();
	materialMap_->flags = {0.0f, 0.0f, 0.0f, 0.0f};

	SetRect(0.0f, 0.0f, 100.0f, 100.0f);
	SetUV({0.0f, 0.0f}, {1.0f, 1.0f});

	textureIndex_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
	initialized_ = true;
}

void UIQuad::SetRect(float x, float y, float width, float height, float rotation) {
	if (!vertexMap_) {
		return;
	}
	const float left = x;
	const float top = y;
	const float right = x + width;
	const float bottom = y + height;

	if (rotation == 0.0f) {
		vertexMap_[0].position = {left, top, 0.0f, 1.0f};      // 左上
		vertexMap_[1].position = {right, top, 0.0f, 1.0f};     // 右上
		vertexMap_[2].position = {left, bottom, 0.0f, 1.0f};   // 左下
		vertexMap_[3].position = {right, bottom, 0.0f, 1.0f};  // 右下
	} else {
		// 矩形の中心まわりに4隅を回す。UIはY下向きなので、正の角度は画面上で時計回りに見える。
		const float centerX = x + width * 0.5f;
		const float centerY = y + height * 0.5f;
		const float cosine = std::cos(rotation);
		const float sine = std::sin(rotation);
		auto rotate = [&](float pointX, float pointY) {
			const float deltaX = pointX - centerX;
			const float deltaY = pointY - centerY;
			return Vector4{centerX + deltaX * cosine - deltaY * sine, centerY + deltaX * sine + deltaY * cosine, 0.0f, 1.0f};
		};
		vertexMap_[0].position = rotate(left, top);
		vertexMap_[1].position = rotate(right, top);
		vertexMap_[2].position = rotate(left, bottom);
		vertexMap_[3].position = rotate(right, bottom);
	}
	for (int i = 0; i < 4; ++i) {
		vertexMap_[i].normal = {0.0f, 0.0f, -1.0f};
	}
}

void UIQuad::SetUVTransform(const Vector2& translate, const Vector2& scale, float rotation) {
	if (!materialMap_) {
		return;
	}
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix({scale.x, scale.y, 1.0f});
	uvTransformMatrix = uvTransformMatrix * MakeRotateZMatrix(rotation);
	uvTransformMatrix = uvTransformMatrix * MakeTranslateMatrix({translate.x, translate.y, 0.0f});
	materialMap_->uvTransform = uvTransformMatrix;
}

void UIQuad::SetUV(const Vector2& uvMin, const Vector2& uvMax) {
	if (!vertexMap_) {
		return;
	}
	vertexMap_[0].texcoord = {uvMin.x, uvMin.y};
	vertexMap_[1].texcoord = {uvMax.x, uvMin.y};
	vertexMap_[2].texcoord = {uvMin.x, uvMax.y};
	vertexMap_[3].texcoord = {uvMax.x, uvMax.y};
}

void UIQuad::SetColor(const Vector4& color) {
	if (materialMap_) {
		materialMap_->color = color;
	}
}

void UIQuad::SetAlphaAsCoverage(bool enabled) {
	if (materialMap_) {
		materialMap_->flags.x = enabled ? 1.0f : 0.0f;
	}
}

void UIQuad::Draw() {
	if (!initialized_) {
		return;
	}

	// Overlay: オルソ / World Space: Canvas配置 x カメラVP。パイプラインもUIRendererが選ぶ。
	*transformMap_ = UIRenderer::GetTransform();

	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	GraphicsPipeline::GetInstance()->SetCommandList(UIRenderer::GetPipelineType(), blendMode_);

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandle(textureIndex_));
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

} // namespace KujataEngine
