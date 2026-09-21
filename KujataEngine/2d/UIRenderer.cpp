#include "UIRenderer.h"

#include "../base/DirectXCommon.h"
#include "../math/MathUtil.h"

namespace KujataEngine {
namespace {

Matrix4x4 gUITransform = MakeIdentity();
PipelineType gUIPipelineType = PipelineType::kUI;
float gTargetWidth = 0.0f;
float gTargetHeight = 0.0f;

// ビューポート/シザー/トポロジ/ヒープの積み込み。Overlay・World共通。
// targetWidth/Height はUIの座標の大きさ、viewportWidth/Height は実際の描画先の大きさ
// (World Space Canvas はドット絵化で小さくなったGameの描画先へ描くので、2つが違うことがある)。
void SetupRenderState(float targetWidth, float targetHeight, float viewportWidth, float viewportHeight) {
	gTargetWidth = targetWidth;
	gTargetHeight = targetHeight;

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	ID3D12DescriptorHeap* descriptorHeaps[] = {dxCommon->GetSrvDescriptorHeap()};
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	D3D12_VIEWPORT viewport{};
	viewport.Width = viewportWidth;
	viewport.Height = viewportHeight;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = static_cast<LONG>(viewportWidth);
	scissorRect.bottom = static_cast<LONG>(viewportHeight);
	commandList->RSSetScissorRects(1, &scissorRect);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace

void UIRenderer::Begin(float targetWidth, float targetHeight) {
	// Screen Space はポスト後の出力へ描くので、UIの座標と描画先の大きさが同じ。
	SetupRenderState(targetWidth, targetHeight, targetWidth, targetHeight);
	// 左上原点のスクリーン空間オルソ(y下方向)。ピクセル座標をそのまま頂点に使う。
	gUITransform = MakeOrthographicMatrix(0.0f, 0.0f, targetWidth, targetHeight, 0.0f, 100.0f);
	gUIPipelineType = PipelineType::kUI;
}

void UIRenderer::BeginWorld(const Matrix4x4& canvasToClip, float targetWidth, float targetHeight) {
	// World Space は3Dと同じ描画先(ドット絵化していれば小さい)へ描く。クリップ座標で描くので、ビューポートを描画先に合わせれば位置はずれない。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	SetupRenderState(targetWidth, targetHeight, static_cast<float>(dxCommon->GetCurrentTargetWidth()), static_cast<float>(dxCommon->GetCurrentTargetHeight()));
	gUITransform = canvasToClip;
	// world空間UIは3Dに遮蔽されるが深度は書かない(スプライトと同じ扱い)。
	gUIPipelineType = PipelineType::kSprite2D;
}

void UIRenderer::End() {}

const Matrix4x4& UIRenderer::GetTransform() { return gUITransform; }

PipelineType UIRenderer::GetPipelineType() { return gUIPipelineType; }

float UIRenderer::GetTargetWidth() { return gTargetWidth; }

float UIRenderer::GetTargetHeight() { return gTargetHeight; }

} // namespace KujataEngine
