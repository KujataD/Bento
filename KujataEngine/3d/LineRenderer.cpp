#include "LineRenderer.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "../base/DirectXCommon.h"
#include <algorithm>
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

	uint32_t viewIndex = DirectXCommon::GetInstance()->GetRenderViewIndex();
	ViewBuffers& buffers = views_[viewIndex < kViewCount ? viewIndex : 0];
	EnsureConstantBuffer(buffers);
	EnsureVertexCapacity(buffers, static_cast<uint32_t>(vertices_.size()));

	std::memcpy(buffers.vertexMap, vertices_.data(), sizeof(LineVertex) * vertices_.size());

	// LineRendererはワールド座標をそのまま受け取るため、Worldは単位行列としてVPだけを送る。
	*buffers.wvpMap = camera.matView * camera.matProjection;

	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	GraphicsPipeline::GetInstance()->SetCommandList(PipelineType::kLine, BlendMode::kNormal);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	commandList->IASetVertexBuffers(0, 1, &buffers.vertexBufferView);
	commandList->SetGraphicsRootConstantBufferView(0, buffers.wvpResource->GetGPUVirtualAddress());
	commandList->DrawInstanced(static_cast<uint32_t>(vertices_.size()), 1, 0, 0);

	Clear();
}

void LineRenderer::Clear() {
	vertices_.clear();
}

void LineRenderer::EnsureVertexCapacity(ViewBuffers& buffers, uint32_t vertexCount) {
	if (vertexCount <= buffers.vertexCapacity && buffers.vertexResource) {
		return;
	}

	// 足りなくなったら倍にして作り直す(毎フレーム少しずつ増えるたびに作り直さないように)。
	// 作り直すのは、このフレームでまだ使っていないこのビューのバッファだけなので、積んだ描画が消えたバッファを指すことはない。
	buffers.vertexCapacity = (std::max)(vertexCount, buffers.vertexCapacity * 2);
	buffers.vertexResource.Reset();
	buffers.vertexMap = nullptr;

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	buffers.vertexResource.Attach(dxCommon->CreateBufferResource(sizeof(LineVertex) * buffers.vertexCapacity));

	buffers.vertexBufferView.BufferLocation = buffers.vertexResource->GetGPUVirtualAddress();
	buffers.vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(LineVertex) * buffers.vertexCapacity);
	buffers.vertexBufferView.StrideInBytes = sizeof(LineVertex);

	HRESULT hr = buffers.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&buffers.vertexMap));
	assert(SUCCEEDED(hr));
}

void LineRenderer::EnsureConstantBuffer(ViewBuffers& buffers) {
	if (buffers.wvpResource) {
		return;
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	size_t constantBufferSize = (sizeof(Matrix4x4) + 0xff) & ~static_cast<size_t>(0xff);
	buffers.wvpResource.Attach(dxCommon->CreateBufferResource(constantBufferSize));

	HRESULT hr = buffers.wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&buffers.wvpMap));
	assert(SUCCEEDED(hr));
	*buffers.wvpMap = MakeIdentity();
}

void DrawLine(const Vector3& start, const Vector3& end, const Vector4& color) {
	LineRenderer::DrawLine(start, end, color);
}

void DrawLine(const Segment& segment, const Vector4& color) {
	LineRenderer::DrawLine(segment, color);
}

} // namespace KujataEngine
