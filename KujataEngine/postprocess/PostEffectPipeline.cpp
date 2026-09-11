#include "PostEffectPipeline.h"

#include "../3d/GraphicsPipeline.h"
#include "../base/DirectXCommon.h"
#include <cassert>

namespace KujataEngine {

// ルート定数のDWORD数はHLSL側PostConstantsと一致している必要がある。
static_assert(sizeof(PostConstants) == 20 * sizeof(uint32_t), "PostConstants must match PostEffect.hlsli layout (20 DWORDs).");
// FogConstantsも同様にshader/Fog.PS.hlslのFogConstantsと一致させること。
static_assert(sizeof(FogConstants) == 32 * sizeof(uint32_t), "FogConstants must match Fog.PS.hlsl layout (32 DWORDs).");

PostEffectPipeline* PostEffectPipeline::GetInstance() {
	static PostEffectPipeline instance;
	return &instance;
}

void PostEffectPipeline::Initialize() {
	CreateRootSignature();
	CreatePipelineStates();
	initialized_ = true;
}

void PostEffectPipeline::CreateRootSignature() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	// [0] t0 主入力テクスチャ / [1] t1 副入力(Tonemapのブルーム) / [2] b0 ルート定数。
	// 1フレーム内で2ビュー×複数パス分の定数が必要なため、CBVではなくルート定数で直接積む(リング管理不要)。
	D3D12_DESCRIPTOR_RANGE descriptorRange0[1] = {};
	descriptorRange0[0].BaseShaderRegister = 0;
	descriptorRange0[0].NumDescriptors = 1;
	descriptorRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE descriptorRange1[1] = {};
	descriptorRange1[0].BaseShaderRegister = 1;
	descriptorRange1[0].NumDescriptors = 1;
	descriptorRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[5] = {};
	rootParameters[kRootParamTexture0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParamTexture0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParamTexture0].DescriptorTable.pDescriptorRanges = descriptorRange0;
	rootParameters[kRootParamTexture0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange0);

	rootParameters[kRootParamTexture1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParamTexture1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParamTexture1].DescriptorTable.pDescriptorRanges = descriptorRange1;
	rootParameters[kRootParamTexture1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange1);

	rootParameters[kRootParamConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParamConstants].Constants.ShaderRegister = 0;
	rootParameters[kRootParamConstants].Constants.Num32BitValues = sizeof(PostConstants) / sizeof(uint32_t);

	// Fogパス専用(b1)。他パスのシェーダーはb1を参照しないため共有RootSignatureに足しても影響しない。
	rootParameters[kRootParamFogConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[kRootParamFogConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParamFogConstants].Constants.ShaderRegister = 1;
	rootParameters[kRootParamFogConstants].Constants.Num32BitValues = sizeof(FogConstants) / sizeof(uint32_t);

	// Fogパス専用(b4)。SpotLightの既存定数バッファをそのまま指すroot descriptor CBV。
	rootParameters[kRootParamSpotLightCBV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParamSpotLightCBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParamSpotLightCBV].Descriptor.ShaderRegister = 4;

	// ダウン/アップサンプルはバイリニア前提。画面端のはみ出しはCLAMPで抑える。
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	// 頂点バッファを使わない(SV_VertexIDのみ)ため入力アセンブラは不要。
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

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

void PostEffectPipeline::CreatePipelineStates() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();
	GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

	// フルスクリーン三角形VSは全パス共通。
	IDxcBlob* vertexShaderBlob = graphicsPipeline->CompileShader(L"shader/Fullscreen.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	struct PassDesc {
		PostEffectType type;
		const wchar_t* pixelShaderPath;
		DXGI_FORMAT rtvFormat;
		bool additiveBlend;
	};
	const PassDesc passes[] = {
	    {PostEffectType::kFog,             L"shader/Fog.PS.hlsl",             DirectXCommon::kSceneColorFormat,    false},
	    {PostEffectType::kBloomDownsample, L"shader/BloomDownsample.PS.hlsl", kBloomFormat,                        false},
	    {PostEffectType::kBloomUpsample,   L"shader/BloomUpsample.PS.hlsl",   kBloomFormat,                        true },
	    {PostEffectType::kTonemap,         L"shader/Tonemap.PS.hlsl",         DirectXCommon::kResolveColorFormat,  false},
	};

	for (const PassDesc& pass : passes) {
		IDxcBlob* pixelShaderBlob = graphicsPipeline->CompileShader(pass.pixelShaderPath, L"ps_6_0");
		assert(pixelShaderBlob != nullptr);

		// フルスクリーンパスなので深度・カリング・入力レイアウトは全て無し。
		D3D12_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

		D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = false;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		D3D12_BLEND_DESC blendDesc{};
		auto& renderTarget = blendDesc.RenderTarget[0];
		renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		if (pass.additiveBlend) {
			// アップサンプルは1段上のダウンサンプル結果へ加算合成する(ONE/ONE)。
			renderTarget.BlendEnable = TRUE;
			renderTarget.SrcBlend = D3D12_BLEND_ONE;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
			renderTarget.DestBlendAlpha = D3D12_BLEND_ZERO;
			renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
		graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
		graphicsPipelineStateDesc.BlendState = blendDesc;
		graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
		graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
		graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
		graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
		graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		graphicsPipelineStateDesc.NumRenderTargets = 1;
		graphicsPipelineStateDesc.RTVFormats[0] = pass.rtvFormat;
		graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		graphicsPipelineStateDesc.SampleDesc.Count = 1;
		graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(pass.type)]));
		assert(SUCCEEDED(hr));

		pixelShaderBlob->Release();
	}

	vertexShaderBlob->Release();
}

void PostEffectPipeline::SetCommandList(PostEffectType type, const PostConstants& constants) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineStates_[static_cast<int32_t>(type)].Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->SetGraphicsRoot32BitConstants(kRootParamConstants, sizeof(PostConstants) / sizeof(uint32_t), &constants, 0);
}

void PostEffectPipeline::SetFogConstants(const FogConstants& constants, D3D12_GPU_VIRTUAL_ADDRESS spotLightCbvAddress) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	commandList->SetGraphicsRoot32BitConstants(kRootParamFogConstants, sizeof(FogConstants) / sizeof(uint32_t), &constants, 0);
	commandList->SetGraphicsRootConstantBufferView(kRootParamSpotLightCBV, spotLightCbvAddress);
}

} // namespace KujataEngine
