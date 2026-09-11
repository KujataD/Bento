#include "ShadowPipeline.h"

#include <cassert>

#include "../3d/GraphicsPipeline.h"
#include "../base/DirectXCommon.h"

namespace KujataEngine {

ShadowPipeline* ShadowPipeline::GetInstance() {
	static ShadowPipeline instance;
	return &instance;
}

void ShadowPipeline::Initialize() {
	if (initialized_) {
		return;
	}
	CreateRootSignature();
	CreatePipelineState();
	initialized_ = true;
}

void ShadowPipeline::CreateRootSignature() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// 深度を書くだけなので、オブジェクトの行列だけあればよい。
	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[kRootParamTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParamTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[kRootParamTransform].Descriptor.ShaderRegister = 0; // b0

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
}

void ShadowPipeline::CreatePipelineState() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();
	GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

	// PixelShaderは無し(深度だけ書く)。
	IDxcBlob* vertexShaderBlob = graphicsPipeline->CompileShader(L"shader/ShadowDepth.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	// 頂点バッファのstrideはVBV側が持つので、VSが使うPOSITIONだけ宣言すればよい。
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	// シャドウアクネ(自己遮蔽による縞)対策。傾いた面ほど深く押し込む。
	// 大きすぎると影が接地点から浮く(ピーターパン)ので、絵を見て調整する。
	rasterizerDesc.DepthBias = 1000;
	rasterizerDesc.SlopeScaledDepthBias = 2.0f;
	rasterizerDesc.DepthBiasClamp = 0.0f;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
	pipelineStateDesc.pRootSignature = rootSignature_.Get();
	pipelineStateDesc.InputLayout = inputLayoutDesc;
	pipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	// PSは設定しない(深度専用パス)。
	pipelineStateDesc.RasterizerState = rasterizerDesc;
	pipelineStateDesc.DepthStencilState = depthStencilDesc;
	pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipelineStateDesc.NumRenderTargets = 0; // カラー出力なし
	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateDesc.SampleDesc.Count = 1;
	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	HRESULT hr = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	vertexShaderBlob->Release();
}

void ShadowPipeline::SetCommandList() {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace KujataEngine
