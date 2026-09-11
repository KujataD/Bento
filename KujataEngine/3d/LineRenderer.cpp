#include "LineRenderer.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "../base/DirectXCommon.h"
#include <cassert>
#include <cstring>

namespace KujataEngine {

LineRenderer* LineRenderer::GetInstance() {
	static LineRenderer instance;
	return &instance;
}

void LineRenderer::DrawLine(const Vector3& start, const Vector3& end, const Vector4& color) {
	LineRenderer* renderer = GetInstance();
	renderer->vertices_.push_back({start, color});
	renderer->vertices_.push_back({end, color});
}

void LineRenderer::DrawLine(const Segment& segment, const Vector4& color) {
	DrawLine(segment.origin, segment.origin + segment.diff, color);
}

void LineRenderer::Render(const Camera& camera) {
	if (vertices_.empty()) {
		return;
	}

	EnsureConstantBuffer();
	EnsureVertexCapacity(static_cast<uint32_t>(vertices_.size()));

	std::memcpy(vertexMap_, vertices_.data(), sizeof(LineVertex) * vertices_.size());

	// LineRendererはワールド座標をそのまま受け取るため、Worldは単位行列としてVPだけを送る。
	*wvpMap_ = camera.matView * camera.matProjection;

	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	GraphicsPipeline::GetInstance()->SetCommandList(PipelineType::kLine, BlendMode::kNormal);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
	commandList->DrawInstanced(static_cast<uint32_t>(vertices_.size()), 1, 0, 0);

	Clear();
}

void LineRenderer::Clear() {
	vertices_.clear();
}

void LineRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
	if (vertexCount <= vertexCapacity_ && vertexResource_) {
		return;
	}

	vertexCapacity_ = vertexCount;
	vertexResource_.Reset();
	vertexMap_ = nullptr;

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	vertexResource_.Attach(dxCommon->CreateBufferResource(sizeof(LineVertex) * vertexCapacity_));

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(LineVertex) * vertexCapacity_);
	vertexBufferView_.StrideInBytes = sizeof(LineVertex);

	HRESULT hr = vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap_));
	assert(SUCCEEDED(hr));
}

void LineRenderer::EnsureConstantBuffer() {
	if (wvpResource_) {
		return;
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	size_t constantBufferSize = (sizeof(Matrix4x4) + 0xff) & ~static_cast<size_t>(0xff);
	wvpResource_.Attach(dxCommon->CreateBufferResource(constantBufferSize));

	HRESULT hr = wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpMap_));
	assert(SUCCEEDED(hr));
	*wvpMap_ = MakeIdentity();
}

void DrawLine(const Vector3& start, const Vector3& end, const Vector4& color) {
	LineRenderer::DrawLine(start, end, color);
}

void DrawLine(const Segment& segment, const Vector4& color) {
	LineRenderer::DrawLine(segment, color);
}

} // namespace KujataEngine
