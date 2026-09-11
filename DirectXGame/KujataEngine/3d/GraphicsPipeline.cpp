#include "GraphicsPipeline.h"
#include "../base/DirectXCommon.h"
#include "../base/Logger.h"
#include "../base/ProjectPath.h"
#include "../base/StringUtil.h"
#include "../base/WinApp.h"
#include <cassert>
#include <format>

namespace KujataEngine {

GraphicsPipeline* GraphicsPipeline::GetInstance() {
	static GraphicsPipeline instance;
	return &instance;
}

void GraphicsPipeline::Initialize() {
	InitializeDXC();
	CreateObject3dRootSignature();
	CreateInstancingRootSignature();
	CreateLineRootSignature();
	CreateObject3dPipelineStateObject();
	CreateInstancingPipelineStateObject();
	CreateLinePipelineStateObject();
	// スクリーン空間UI(深度OFF)と world空間2Dスプライト(深度テストのみ)。シェーダーは共通。
	// スクリーン空間UIはポスト(フォグ/ブルーム/トーンマップ)の後にLDR RTへ描くため出力先が異なる。
	CreateUIStyleRootSignature(PipelineType::kUI);
	CreateUIStylePipelineStateObject(PipelineType::kUI, false, true);
	CreateUIStyleRootSignature(PipelineType::kSprite2D);
	CreateUIStylePipelineStateObject(PipelineType::kSprite2D, true, false);
}

void GraphicsPipeline::InitializeDXC() {
	HRESULT hr;

	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// includeに対応するための設定
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));
}

IDxcBlob* GraphicsPipeline::CompileShader(const std::wstring& filePath, const wchar_t* profile) {
	// 呼び出し側は "shader/xxx.hlsl" のようなEngineData相対パスで渡してくるため、
	// カレントディレクトリに依存せずEngineData配下から解決する(ソリューション実行/exe単体実行の両対応)。
	// シェーダーはエンジンの持ち物なので、どのプロジェクトを開いても同じものを使う。
	std::wstring resolvedPath = (GetEngineDataRoot() / filePath).wstring();

	// シェーダーコンパイルする旨をログに出す
	OutputDebugStringW(std::format(L"Begin CompileShader, path: {}, profile: {}\n", resolvedPath, profile).c_str());

	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils_->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容 
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// コンパイルオプション
	LPCWSTR arguments[] = {
	    resolvedPath.c_str(), // コンパイル対象のhlslファイル名
	    L"-E",
	    L"main", // エントリーポイントの指定
	    L"-T",
	    profile, // ShaderProfileの設定
	    L"-Zi",
	    L"-Qembed_debug", // デバッグ用の情報を埋め込む
	    L"-Od",           // 最適化を外しておく
	    L"-Zpr",          // メモリレイアウトは行優先
	};

	// 実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler_->Compile(
	    &shaderSourceBuffer,        // 読み込んだファイル
	    arguments,                  // コンパイルオプション
	    _countof(arguments),        // コンパイルオプションの数
	    includeHandler_,            // includeが含まれた諸々
	    IID_PPV_ARGS(&shaderResult) // コンパイル結果
	);

	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// 3. 警告·エラーがでていないか確認する

	// 警告·エラーが出てたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Logger::Log(shaderError->GetStringPointer());
		// 警告·エラーダメゼッタイ
		assert(false);
	}

	// 4. Compile結果を受け取って返す

	// コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	// 成功したログを出す
	Logger::Log(StringUtil::ToString(std::format(L"Compile Succeeded, path: {}, profile: {}\n", resolvedPath, profile)));
	// もう使わないリソースを解放
	shaderSource->Release();
	shaderResult->Release();
	// 実行用のバイナリを返却
	return shaderBlob;
}

void GraphicsPipeline::CreateObject3dRootSignature() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();
	HRESULT hr;

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameter作成
	// b0 Material
	D3D12_ROOT_PARAMETER rootParameters[10] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う b0のbに対応する bはConstantBuffer
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;                    // レジスタ番号0とバインド b0の0に対応する。もしb11と紐づけたいなら11となる。

	// b0 TransformationMatrix
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0を使う

	// DescriptorRange 複数のDescriptorの設定を一括で行う
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;                                                   // 0から始まる
	descriptorRange[0].NumDescriptors = 1;                                                       // 数は1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う(t)
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

	// t0 Texture
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;      // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;             // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

	// b1 Directional Light
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;                    // レジスタ番号1を使う

	// b2 Camera
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[4].Descriptor.ShaderRegister = 2;                    // レジスタ番号2を使う

	// b3 ポイントライト
	rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[5].Descriptor.ShaderRegister = 3;                    // レジスタ番号3を使う

	// b4 スポットライト
	rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[6].Descriptor.ShaderRegister = 4;                    // レジスタ番号4を使う

	// t1 シャドウマップ(ShadowMapが書いた深度)
	D3D12_DESCRIPTOR_RANGE shadowMapRange[1] = {};
	shadowMapRange[0].BaseShaderRegister = 1; // t1
	shadowMapRange[0].NumDescriptors = 1;
	shadowMapRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowMapRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[7].DescriptorTable.pDescriptorRanges = shadowMapRange;
	rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(shadowMapRange);

	// b5 シャドウ定数(ライトのビュープロジェクション行列/バイアス/テクセルサイズ)
	rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[8].Descriptor.ShaderRegister = 5;

	// t2 エミッションマップ(自己発光の分布)。未指定のマテリアルには白1x1が入るので、
	// シェーダー側は常に乗算するだけでよい(マップ有無の分岐が要らない)。
	D3D12_DESCRIPTOR_RANGE emissiveMapRange[1] = {};
	emissiveMapRange[0].BaseShaderRegister = 2; // t2
	emissiveMapRange[0].NumDescriptors = 1;
	emissiveMapRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	emissiveMapRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[9].DescriptorTable.pDescriptorRanges = emissiveMapRange;
	rootParameters[9].DescriptorTable.NumDescriptorRanges = _countof(emissiveMapRange);

	descriptionRootSignature.pParameters = rootParameters;             // ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters); // 配列の長さ

	// Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0~1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う

	// s1 シャドウ用の比較サンプラ。SampleCmpLevelZeroが「サンプルと同時に深度比較」を行い、
	// 4テクセルの比較結果をバイリニア補間して返すため、これだけで影の縁が滑らかになる。
	staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	// シャドウマップの外は「影なし」に倒したいので、境界色を最遠(白)にする。
	staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[1].ShaderRegister = 1;
	staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(PipelineType::kObject3d)]));
	assert(SUCCEEDED(hr));
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(PipelineType::kObject3dWireframe)]));
	assert(SUCCEEDED(hr));
	// 両面/深度書き込みOFFはRootSignatureをkObject3dと共有する(ラスタライザと深度設定だけが違う)。
	rootSignature_[static_cast<int32_t>(PipelineType::kObject3dDoubleSided)] = rootSignature_[static_cast<int32_t>(PipelineType::kObject3d)];
	rootSignature_[static_cast<int32_t>(PipelineType::kObject3dNoDepthWrite)] = rootSignature_[static_cast<int32_t>(PipelineType::kObject3d)];
	rootSignature_[static_cast<int32_t>(PipelineType::kObject3dDoubleSidedNoDepthWrite)] = rootSignature_[static_cast<int32_t>(PipelineType::kObject3d)];

	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
}

void GraphicsPipeline::CreateInstancingRootSignature() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();
	HRESULT hr;

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// DescriptorRange 複数のDescriptorの設定を一括で行う
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;                                                   // 0から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;                                                       // 数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;                              // SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

	// RootParameter作成。複数設定できるので配列。今回は結果1つだけなので長さ1の配列
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う b0のbに対応する bはConstantBuffer
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;                    // レジスタ番号0とバインド b0の0に対応する。もしb11と紐づけたいなら11となる。

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;                   // CBVを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;                            // VertexShaderで使う
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;             //  Tableの中身の配列を指定
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する数

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;                   // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;                             // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;             // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する数

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;                    // レジスタ番号1を使う
	descriptionRootSignature.pParameters = rootParameters;              // ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);  // 配列の長さ

	// Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0~1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(PipelineType::kParticle)]));
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(PipelineType::kInstancingObject3d)]));
	assert(SUCCEEDED(hr));

	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
}

void GraphicsPipeline::CreateLineRootSignature() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();
	HRESULT hr;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	// b0 WVP。Lineは頂点色だけで描くため、TextureやLight用RootParameterは持たない。
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(PipelineType::kLine)]));
	assert(SUCCEEDED(hr));

	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
}

void GraphicsPipeline::CreateObject3dPipelineStateObject() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	// シェーダーをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"shader/Object3D.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"shader/Object3D.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// 2. InputLayoutの設定
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// 3. BlendStateの設定（すべての色要素を書き込む）
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 4. RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;  // 裏面（時計回り）を表示しない
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

	// 7. DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kObject3d)].Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDesc.NumRenderTargets = 2;
	graphicsPipelineStateDesc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	graphicsPipelineStateDesc.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// --- ワイヤーフレーム ---
	// 4. RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDescWireframe{};
	rasterizerDescWireframe.CullMode = D3D12_CULL_MODE_NONE;      // 裏面は表示する
	rasterizerDescWireframe.FillMode = D3D12_FILL_MODE_WIREFRAME; // ワイヤーフレーム表示
	rasterizerDescWireframe.DepthClipEnable = true;               // far範囲外の頂点は描画しない

	D3D12_DEPTH_STENCIL_DESC depthStencilDescWireframe{};
	depthStencilDescWireframe.DepthEnable = true;
	depthStencilDescWireframe.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDescWireframe.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescWireframe{};
	graphicsPipelineStateDescWireframe.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kObject3dWireframe)].Get();
	graphicsPipelineStateDescWireframe.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDescWireframe.BlendState = blendDesc;
	graphicsPipelineStateDescWireframe.RasterizerState = rasterizerDescWireframe;
	graphicsPipelineStateDescWireframe.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescWireframe.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescWireframe.DepthStencilState = depthStencilDescWireframe;
	graphicsPipelineStateDescWireframe.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDescWireframe.NumRenderTargets = 2;
	graphicsPipelineStateDescWireframe.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	graphicsPipelineStateDescWireframe.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	graphicsPipelineStateDescWireframe.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDescWireframe.SampleDesc.Count = 1;
	graphicsPipelineStateDescWireframe.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	for (int32_t i = 0; i < static_cast<int32_t>(BlendMode::kCountOfBlendMode); i++) {
		D3D12_BLEND_DESC blendDesc{};
		auto& renderTarget = blendDesc.RenderTarget[0];

		// 共通初期化部
		renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		renderTarget.BlendEnable = TRUE;

		renderTarget.SrcBlend = D3D12_BLEND_ONE;
		renderTarget.DestBlend = D3D12_BLEND_ZERO;
		renderTarget.BlendOp = D3D12_BLEND_OP_ADD;

		renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
		// **アルファはsource-overで合成する**(dstA = srcA + dstA*(1-srcA))。
		// ZEROにすると dstA = srcA となり、描いた側のαでレンダーターゲットのαが上書きされる。
		// α=0の全画面UIを1枚重ねただけでRT全体が透明になり、
		// RTをαブレンドで表示するエディタのGame/Sceneビューが真っ黒になる
		// (バックバッファ直描きのゲーム単体ビルドではαが無視されるため表面化しない)。
		renderTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;

		switch (static_cast<BlendMode>(i)) {
		case BlendMode::kNone:
			renderTarget.BlendEnable = FALSE;
			break;

		case BlendMode::kNormal:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			break;

		case BlendMode::kAdd:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		case BlendMode::kMultiply:
			renderTarget.SrcBlend = D3D12_BLEND_ZERO;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_SRC_COLOR;
			break;

		case BlendMode::kExclusion:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			break;

		case BlendMode::kScreen:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		case BlendMode::kPremultipliedAlpha:
			// 色があらかじめαを掛けてある前提。フチが暗くならない。
			renderTarget.SrcBlend = D3D12_BLEND_ONE;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			break;

		case BlendMode::kSubtract:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		default:
			break;
		}

		graphicsPipelineStateDesc.BlendState = blendDesc;
		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kObject3d)][i]));

		graphicsPipelineStateDescWireframe.BlendState = blendDesc;
		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDescWireframe, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kObject3dWireframe)][i]));
		assert(SUCCEEDED(hr));

		// --- 両面(背面カリングなし)。それ以外はkObject3dと同一 ---
		D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescDoubleSided = graphicsPipelineStateDesc;
		D3D12_RASTERIZER_DESC rasterizerDescDoubleSided = rasterizerDesc;
		rasterizerDescDoubleSided.CullMode = D3D12_CULL_MODE_NONE;
		graphicsPipelineStateDescDoubleSided.RasterizerState = rasterizerDescDoubleSided;
		graphicsPipelineStateDescDoubleSided.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kObject3dDoubleSided)].Get();
		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDescDoubleSided, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kObject3dDoubleSided)][i]));
		assert(SUCCEEDED(hr));

		// --- 深度書き込みOFF(半透明・加算用)。深度テストは行うので、不透明物には正しく隠される ---
		D3D12_DEPTH_STENCIL_DESC depthStencilDescNoWrite = depthStencilDesc;
		depthStencilDescNoWrite.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC descNoDepthWrite = graphicsPipelineStateDesc;
		descNoDepthWrite.DepthStencilState = depthStencilDescNoWrite;
		descNoDepthWrite.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kObject3dNoDepthWrite)].Get();
		hr = device->CreateGraphicsPipelineState(&descNoDepthWrite, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kObject3dNoDepthWrite)][i]));
		assert(SUCCEEDED(hr));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC descDoubleSidedNoDepthWrite = graphicsPipelineStateDescDoubleSided;
		descDoubleSidedNoDepthWrite.DepthStencilState = depthStencilDescNoWrite;
		descDoubleSidedNoDepthWrite.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kObject3dDoubleSidedNoDepthWrite)].Get();
		hr = device->CreateGraphicsPipelineState(&descDoubleSidedNoDepthWrite, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kObject3dDoubleSidedNoDepthWrite)][i]));
		assert(SUCCEEDED(hr));
	}

	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
}

void GraphicsPipeline::CreateLinePipelineStateObject() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	IDxcBlob* vertexShaderBlob = CompileShader(L"shader/Line.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"shader/Line.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "COLOR";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.DepthClipEnable = true;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kLine)].Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDesc.NumRenderTargets = 2;
	graphicsPipelineStateDesc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	graphicsPipelineStateDesc.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	for (int32_t i = 0; i < static_cast<int32_t>(BlendMode::kCountOfBlendMode); i++) {
		D3D12_BLEND_DESC blendDesc{};
		auto& renderTarget = blendDesc.RenderTarget[0];

		renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		renderTarget.BlendEnable = TRUE;
		renderTarget.SrcBlend = D3D12_BLEND_ONE;
		renderTarget.DestBlend = D3D12_BLEND_ZERO;
		renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
		renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
		// **アルファはsource-overで合成する**(dstA = srcA + dstA*(1-srcA))。
		// ZEROにすると dstA = srcA となり、描いた側のαでレンダーターゲットのαが上書きされる。
		// α=0の全画面UIを1枚重ねただけでRT全体が透明になり、
		// RTをαブレンドで表示するエディタのGame/Sceneビューが真っ黒になる
		// (バックバッファ直描きのゲーム単体ビルドではαが無視されるため表面化しない)。
		renderTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;

		switch (static_cast<BlendMode>(i)) {
		case BlendMode::kNone:
			renderTarget.BlendEnable = FALSE;
			break;
		case BlendMode::kNormal:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			break;
		case BlendMode::kAdd:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;
		case BlendMode::kSubtract:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			renderTarget.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			break;
		case BlendMode::kMultiply:
			renderTarget.SrcBlend = D3D12_BLEND_ZERO;
			renderTarget.DestBlend = D3D12_BLEND_SRC_COLOR;
			break;
		case BlendMode::kScreen:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;
		case BlendMode::kExclusion:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
			break;
		default:
			break;
		}

		graphicsPipelineStateDesc.BlendState = blendDesc;
		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kLine)][i]));
		assert(SUCCEEDED(hr));
	}

	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
}

void GraphicsPipeline::CreateInstancingPipelineStateObject() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	// シェーダーをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"shader/Particle.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"shader/Particle.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// 2. InputLayoutの設定
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// 3. BlendStateの設定（すべての色要素を書き込む）
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 4. RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;  // 裏面（時計回り）を表示しない
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

	D3D12_RASTERIZER_DESC rasterizerDescParticle{};
	rasterizerDescParticle.CullMode = D3D12_CULL_MODE_NONE;  // 裏面（時計回り）を表示しない
	rasterizerDescParticle.FillMode = D3D12_FILL_MODE_SOLID; // 三角形の中を塗りつぶす

	// 7. DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDescParticle{};
	depthStencilDescParticle.DepthEnable = true;
	depthStencilDescParticle.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Depthの書き込みを行わない
	depthStencilDescParticle.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_DEPTH_STENCIL_DESC depthStencilDescInstancingObject3d{}; 
	depthStencilDescInstancingObject3d.DepthEnable = true;
	depthStencilDescInstancingObject3d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // Depthの書き込みを行う
	depthStencilDescInstancingObject3d.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescParticle{};
	graphicsPipelineStateDescParticle.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kParticle)].Get();
	graphicsPipelineStateDescParticle.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDescParticle.BlendState = blendDesc;
	graphicsPipelineStateDescParticle.RasterizerState = rasterizerDescParticle;
	graphicsPipelineStateDescParticle.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescParticle.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescParticle.DepthStencilState = depthStencilDescParticle;
	graphicsPipelineStateDescParticle.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDescParticle.NumRenderTargets = 2;
	graphicsPipelineStateDescParticle.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	graphicsPipelineStateDescParticle.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	graphicsPipelineStateDescParticle.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDescParticle.SampleDesc.Count = 1;
	graphicsPipelineStateDescParticle.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// PSOの生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescInstancingObject3d{};
	graphicsPipelineStateDescInstancingObject3d.pRootSignature = rootSignature_[static_cast<int32_t>(PipelineType::kInstancingObject3d)].Get();
	graphicsPipelineStateDescInstancingObject3d.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDescInstancingObject3d.BlendState = blendDesc;
	graphicsPipelineStateDescInstancingObject3d.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDescInstancingObject3d.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescInstancingObject3d.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDescInstancingObject3d.DepthStencilState = depthStencilDescInstancingObject3d;
	graphicsPipelineStateDescInstancingObject3d.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDescInstancingObject3d.NumRenderTargets = 2;
	graphicsPipelineStateDescInstancingObject3d.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	graphicsPipelineStateDescInstancingObject3d.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	graphicsPipelineStateDescInstancingObject3d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDescInstancingObject3d.SampleDesc.Count = 1;
	graphicsPipelineStateDescInstancingObject3d.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	for (int32_t i = 0; i < static_cast<int32_t>(BlendMode::kCountOfBlendMode); i++) {
		D3D12_BLEND_DESC blendDesc{};
		auto& renderTarget = blendDesc.RenderTarget[0];

		// 共通初期化部
		renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		renderTarget.BlendEnable = TRUE;

		renderTarget.SrcBlend = D3D12_BLEND_ONE;
		renderTarget.DestBlend = D3D12_BLEND_ZERO;
		renderTarget.BlendOp = D3D12_BLEND_OP_ADD;

		renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
		// **アルファはsource-overで合成する**(dstA = srcA + dstA*(1-srcA))。
		// ZEROにすると dstA = srcA となり、描いた側のαでレンダーターゲットのαが上書きされる。
		// α=0の全画面UIを1枚重ねただけでRT全体が透明になり、
		// RTをαブレンドで表示するエディタのGame/Sceneビューが真っ黒になる
		// (バックバッファ直描きのゲーム単体ビルドではαが無視されるため表面化しない)。
		renderTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;

		switch (static_cast<BlendMode>(i)) {
		case BlendMode::kNone:
			renderTarget.BlendEnable = FALSE;
			break;

		case BlendMode::kNormal:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			break;

		case BlendMode::kAdd:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		case BlendMode::kMultiply:
			renderTarget.SrcBlend = D3D12_BLEND_ZERO;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_SRC_COLOR;
			break;

		case BlendMode::kExclusion:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			break;

		case BlendMode::kScreen:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		case BlendMode::kSubtract:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;

		default:
			break;
		}

		graphicsPipelineStateDescParticle.BlendState = blendDesc;
		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDescParticle, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kParticle)][i]));

		graphicsPipelineStateDescInstancingObject3d.BlendState = blendDesc;
		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDescInstancingObject3d, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(PipelineType::kInstancingObject3d)][i]));
		assert(SUCCEEDED(hr));
	}

	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
}

void GraphicsPipeline::CreateUIStyleRootSignature(PipelineType pipelineType) {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// [0] b0 Material(PS), [1] b0 Transform(VS), [2] t0 Texture(PS)。ライト非依存。
	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// UIもWRAPサンプラー(0~1の範囲外をリピート)。ImageのUV Scale/Offsetによるタイリング・スクロールを可能にする。
	// フォントアトラス等はUVが0~1内に収まるため影響しない。
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_[static_cast<int32_t>(pipelineType)]));
	assert(SUCCEEDED(hr));

	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
}

void GraphicsPipeline::CreateUIStylePipelineStateObject(PipelineType pipelineType, bool depthTestEnabled, bool ldrTarget) {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	IDxcBlob* vertexShaderBlob = CompileShader(L"shader/UI.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);
	IDxcBlob* pixelShaderBlob = CompileShader(L"shader/UI.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// 入力レイアウトはObject3dと共通(VertexData: POSITION/TEXCOORD/NORMAL)。
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// UI・2Dスプライトとも両面表示(負のスケールで反転しても消えないようにする)。
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// kUI      : 深度無効(オーバーレイなので常に上書き)。
	// kSprite2D: 深度テストのみ有効で書き込みはしない(半透明キュー相当)。
	//            3Dオブジェクトに遮蔽はされるが、スプライト同士は深度で争わず描画順で前後が決まる。
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = depthTestEnabled;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = depthTestEnabled ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_[static_cast<int32_t>(pipelineType)].Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	if (ldrTarget) {
		// ポスト適用後のLDR RT(Resolve RT / バックバッファ)へ直接描く。深度もエミッションRTも無い。
		graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		graphicsPipelineStateDesc.NumRenderTargets = 1;
		graphicsPipelineStateDesc.RTVFormats[0] = DirectXCommon::kResolveColorFormat;
	} else {
		// HDRシーンRT(MRT: カラー+エミッション)。ポストの入力になる。
		graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		graphicsPipelineStateDesc.NumRenderTargets = 2;
		graphicsPipelineStateDesc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
		graphicsPipelineStateDesc.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	}
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	for (int32_t i = 0; i < static_cast<int32_t>(BlendMode::kCountOfBlendMode); i++) {
		D3D12_BLEND_DESC blendDesc{};
		auto& renderTarget = blendDesc.RenderTarget[0];
		renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		renderTarget.BlendEnable = TRUE;
		renderTarget.SrcBlend = D3D12_BLEND_ONE;
		renderTarget.DestBlend = D3D12_BLEND_ZERO;
		renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
		renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
		// **アルファはsource-overで合成する**(dstA = srcA + dstA*(1-srcA))。
		// ZEROにすると dstA = srcA となり、描いた側のαでレンダーターゲットのαが上書きされる。
		// α=0の全画面UIを1枚重ねただけでRT全体が透明になり、
		// RTをαブレンドで表示するエディタのGame/Sceneビューが真っ黒になる
		// (バックバッファ直描きのゲーム単体ビルドではαが無視されるため表面化しない)。
		renderTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;

		switch (static_cast<BlendMode>(i)) {
		case BlendMode::kNone:
			renderTarget.BlendEnable = FALSE;
			break;
		case BlendMode::kNormal:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			break;
		case BlendMode::kAdd:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;
		case BlendMode::kMultiply:
			renderTarget.SrcBlend = D3D12_BLEND_ZERO;
			renderTarget.DestBlend = D3D12_BLEND_SRC_COLOR;
			break;
		case BlendMode::kExclusion:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
			break;
		case BlendMode::kScreen:
			renderTarget.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;
		case BlendMode::kSubtract:
			renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			renderTarget.DestBlend = D3D12_BLEND_ONE;
			break;
		case BlendMode::kPremultipliedAlpha:
			// 乗算済みαブレンド。テキストのアンチエイリアス縁が暗くならない。
			renderTarget.SrcBlend = D3D12_BLEND_ONE;
			renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
			break;
		default:
			break;
		}

		graphicsPipelineStateDesc.BlendState = blendDesc;
		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(pipelineType)][i]));
		assert(SUCCEEDED(hr));
	}

	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
}

void GraphicsPipeline::SetCommandList(PipelineType pipelineType, BlendMode blendMode) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// RootSignatureとPSOを設定
	commandList->SetGraphicsRootSignature(rootSignature_[static_cast<int32_t>(pipelineType)].Get());
	commandList->SetPipelineState(pipelineStates_[static_cast<int32_t>(pipelineType)][static_cast<int32_t>(blendMode)].Get());
}

void GraphicsPipeline::Finalize() {
	if (includeHandler_) {
		includeHandler_->Release();
		includeHandler_ = nullptr;
	}
	if (dxcCompiler_) {
		dxcCompiler_->Release();
		dxcCompiler_ = nullptr;
	}
	if (dxcUtils_) {
		dxcUtils_->Release();
		dxcUtils_ = nullptr;
	}
}

} // namespace KujataEngine
