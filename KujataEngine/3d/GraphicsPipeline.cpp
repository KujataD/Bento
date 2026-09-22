#include "GraphicsPipeline.h"
#include "../base/DirectXCommon.h"
#include "../base/Logger.h"
#include "../base/ProjectPath.h"
#include "../base/StringUtil.h"
#include "../base/WinApp.h"
#include <cassert>
#include <cctype>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace KujataEngine {

namespace {

// Object3d系の頂点の並び(VertexData と一致させる)。PSOの設定から指すので、関数を抜けても消えない場所に置く。
const D3D12_INPUT_ELEMENT_DESC kObject3dInputElements[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// Object3d系のPSOの種類(自作シェーダーもこの5種類ぶん作れる)。
bool IsObject3dPipelineType(PipelineType pipelineType) {
	return pipelineType == PipelineType::kObject3d || pipelineType == PipelineType::kObject3dWireframe || pipelineType == PipelineType::kObject3dDoubleSided ||
	       pipelineType == PipelineType::kObject3dNoDepthWrite || pipelineType == PipelineType::kObject3dDoubleSidedNoDepthWrite;
}

// 合成方法ごとのブレンド設定。
D3D12_BLEND_DESC MakeBlendDesc(BlendMode blendMode) {
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

	switch (blendMode) {
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
	return blendDesc;
}

// 自作シェーダーを探す鍵。同じファイルを別の書き方で指しても同じ番号になるよう、正規化して小文字にそろえる。
std::string MakeShaderKey(const std::filesystem::path& path) {
	std::error_code errorCode;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, errorCode);
	if (errorCode) {
		normalized = path.lexically_normal();
	}
	std::string key = normalized.generic_string();
	for (char& character : key) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return key;
}

} // namespace


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
	// エンジンのシェーダーは壊れていたら先へ進めないので、警告も含めて何か出たら止める。
	std::string errors;
	IDxcBlob* shaderBlob = TryCompileShader(GetEngineDataRoot() / filePath, L"main", profile, nullptr, errors);
	if (!shaderBlob || !errors.empty()) {
		Logger::Log(errors);
		// 警告·エラーダメゼッタイ
		assert(false);
	}
	return shaderBlob;
}

IDxcBlob* GraphicsPipeline::TryCompileShader(const std::filesystem::path& absolutePath, const wchar_t* entryPoint, const wchar_t* profile, const wchar_t* define,
                                             std::string& errors) {
	errors.clear();
	const std::wstring resolvedPath = absolutePath.wstring();

	// シェーダーコンパイルする旨をログに出す
	OutputDebugStringW(std::format(L"Begin CompileShader, path: {}, entry: {}, profile: {}\n", resolvedPath, entryPoint, profile).c_str());

	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils_->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
	if (FAILED(hr) || !shaderSource) {
		errors = "ファイルを開けません: " + absolutePath.generic_string();
		return nullptr;
	}

	// 読み込んだファイルの内容
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// エンジンのシェーダーフォルダを include の探し先に足す(Data 配下の自作シェーダーが "Object3dCustom.hlsli" を読めるように)。
	const std::wstring engineShaderDirectory = (GetEngineDataRoot() / "shader").wstring();

	// コンパイルオプション
	std::vector<LPCWSTR> arguments = {
	    resolvedPath.c_str(), // コンパイル対象のhlslファイル名
	    L"-E",
	    entryPoint, // エントリーポイントの指定
	    L"-T",
	    profile, // ShaderProfileの設定
	    L"-I",
	    engineShaderDirectory.c_str(),
	    L"-Zi",
	    L"-Qembed_debug", // デバッグ用の情報を埋め込む
	    L"-Od",           // 最適化を外しておく
	    L"-Zpr",          // メモリレイアウトは行優先
	};
	if (define) {
		arguments.push_back(L"-D");
		arguments.push_back(define);
	}

	// 実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler_->Compile(
	    &shaderSourceBuffer,                   // 読み込んだファイル
	    arguments.data(),                      // コンパイルオプション
	    static_cast<UINT32>(arguments.size()), // コンパイルオプションの数
	    includeHandler_,                       // includeが含まれた諸々
	    IID_PPV_ARGS(&shaderResult)            // コンパイル結果
	);
	shaderSource->Release();
	if (FAILED(hr) || !shaderResult) {
		// コンパイルエラーではなくdxcが起動できないなど致命的な状況
		errors = "シェーダーコンパイラを動かせません: " + absolutePath.generic_string();
		return nullptr;
	}

	// 警告·エラーの文面(警告だけでもここに入る)
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError) {
		if (shaderError->GetStringLength() != 0) {
			errors = shaderError->GetStringPointer();
		}
		shaderError->Release();
	}

	HRESULT status = S_OK;
	shaderResult->GetStatus(&status);
	IDxcBlob* shaderBlob = nullptr;
	if (SUCCEEDED(status)) {
		// コンパイル結果から実行用のバイナリ部分を取得
		shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	}
	shaderResult->Release();
	if (!shaderBlob) {
		if (errors.empty()) {
			errors = "コンパイルに失敗しました: " + absolutePath.generic_string();
		}
		return nullptr;
	}
	// 成功したログを出す
	Logger::Log(StringUtil::ToString(std::format(L"Compile Succeeded, path: {}, entry: {}, profile: {}\n", resolvedPath, entryPoint, profile)));
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

	// t2 エミッションマップ(自己発光の分布)。未指定のマテリアルには白1x1が入るので、
	// シェーダー側は常に乗算するだけでよい(マップ有無の分岐が要らない)。
	D3D12_DESCRIPTOR_RANGE emissiveMapRange[1] = {};
	emissiveMapRange[0].BaseShaderRegister = 2; // t2
	emissiveMapRange[0].NumDescriptors = 1;
	emissiveMapRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	emissiveMapRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[7].DescriptorTable.pDescriptorRanges = emissiveMapRange;
	rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(emissiveMapRange);

	// b5 シェーダーパラメータ(マテリアルの Shader Params と時間)。自作シェーダーが頂点・ピクセルの両方から読む。
	rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[8].Descriptor.ShaderRegister = 5;

	// 不透明物を描き終えた時点の深度のコピー(t3。自作シェーダーの gSceneDepth)。未取得なら白(=いちばん遠い)が入る。
	D3D12_DESCRIPTOR_RANGE sceneDepthRange[1] = {};
	sceneDepthRange[0].BaseShaderRegister = 3; // t3
	sceneDepthRange[0].NumDescriptors = 1;
	sceneDepthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	sceneDepthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	rootParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[9].DescriptorTable.pDescriptorRanges = sceneDepthRange;
	rootParameters[9].DescriptorTable.NumDescriptorRanges = _countof(sceneDepthRange);

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
	// s1: ぼかさずに読む(ポイントサンプリング)。マテリアルの Point Sampling で選ぶ。
	// 粗いテクスチャをドットのまま見せるため(ローポリ・ボクセル調)。
	staticSamplers[1] = staticSamplers[0];
	staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	staticSamplers[1].ShaderRegister = 1;

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

D3D12_GRAPHICS_PIPELINE_STATE_DESC GraphicsPipeline::MakeObject3dPipelineDesc(PipelineType pipelineType, BlendMode blendMode, D3D12_SHADER_BYTECODE vertexShader,
                                                                              D3D12_SHADER_BYTECODE pixelShader) const {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_[static_cast<int32_t>(pipelineType)].Get();
	desc.InputLayout = {kObject3dInputElements, _countof(kObject3dInputElements)};
	desc.BlendState = MakeBlendDesc(blendMode);
	desc.VS = vertexShader;
	desc.PS = pixelShader;

	// ラスタライザ: 通常は裏面(時計回り)を描かない。両面は裏も描く。ワイヤーは線で描き、far範囲外の頂点は描かない。
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	if (pipelineType == PipelineType::kObject3dDoubleSided || pipelineType == PipelineType::kObject3dDoubleSidedNoDepthWrite) {
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	}
	if (pipelineType == PipelineType::kObject3dWireframe) {
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
		rasterizerDesc.DepthClipEnable = true;
	}
	desc.RasterizerState = rasterizerDesc;

	// 深度: テストは常に行う。深度書き込みOFF版(半透明・加算用)は書かない(不透明物には正しく隠される)。
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	if (pipelineType == PipelineType::kObject3dNoDepthWrite || pipelineType == PipelineType::kObject3dDoubleSidedNoDepthWrite) {
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	}
	desc.DepthStencilState = depthStencilDesc;

	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.NumRenderTargets = 2;
	desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
	desc.RTVFormats[1] = DirectXCommon::kSceneEmissionFormat;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	return desc;
}

void GraphicsPipeline::CreateObject3dPipelineStateObject() {
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	// シェーダーをコンパイルする。頂点シェーダーは、VSMain を持たない自作シェーダーも使うので残しておく。
	IDxcBlob* vertexShaderBlob = CompileShader(L"shader/Object3D.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);
	defaultVertexShader_.Attach(vertexShaderBlob);

	IDxcBlob* pixelShaderBlob = CompileShader(L"shader/Object3D.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	const D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	const D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};

	// 種類(通常・ワイヤー・両面・深度書き込みOFF・両面+深度書き込みOFF)× 合成方法 のPSOを全部作る。
	const PipelineType object3dTypes[] = {PipelineType::kObject3d, PipelineType::kObject3dWireframe, PipelineType::kObject3dDoubleSided,
	                                      PipelineType::kObject3dNoDepthWrite, PipelineType::kObject3dDoubleSidedNoDepthWrite};
	for (PipelineType pipelineType : object3dTypes) {
		for (int32_t i = 0; i < static_cast<int32_t>(BlendMode::kCountOfBlendMode); i++) {
			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeObject3dPipelineDesc(pipelineType, static_cast<BlendMode>(i), vertexShader, pixelShader);
			HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStates_[static_cast<int32_t>(pipelineType)][i]));
			assert(SUCCEEDED(hr));
		}
	}

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

uint32_t GraphicsPipeline::AcquireCustomShader(const std::filesystem::path& path) {
	if (path.empty()) {
		return 0;
	}
	const std::string key = MakeShaderKey(path);
	auto found = customShaderIds_.find(key);
	if (found != customShaderIds_.end()) {
		return found->second;
	}

	CustomShader& shader = customShaders_.emplace_back();
	shader.info.path = path;
	const uint32_t shaderId = static_cast<uint32_t>(customShaders_.size());
	customShaderIds_[key] = shaderId;

	std::error_code errorCode;
	if (std::filesystem::exists(path, errorCode)) {
		shader.lastWriteTime = std::filesystem::last_write_time(path, errorCode);
		CompileCustomShader(shader);
	} else {
		shader.info.error = "ファイルがありません: " + path.generic_string();
		Logger::Log("[Shader] 失敗: " + shader.info.error + "\n");
	}
	return shaderId;
}

bool GraphicsPipeline::CompileCustomShader(CustomShader& shader) {
	const std::filesystem::path& path = shader.info.path;

	// VSMain を書いたときだけ頂点シェーダーもコンパイルする(無ければ標準の頂点処理)。
	std::string source;
	{
		std::ifstream file(path, std::ios::binary);
		std::ostringstream stream;
		stream << file.rdbuf();
		source = stream.str();
	}
	// コメントの中の "VSMain" で誤判定しないよう、"VSMain(" の形(関数)を探す。
	static const std::regex kVertexEntry(R"(\bVSMain\s*\()");
	const bool hasVertexShader = std::regex_search(source, kVertexEntry);

	std::string errors;
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShader;
	pixelShader.Attach(TryCompileShader(path, L"PSMain", L"ps_6_0", L"KUJATA_PIXEL_SHADER", errors));
	std::string vertexErrors;
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShader;
	if (pixelShader && hasVertexShader) {
		vertexShader.Attach(TryCompileShader(path, L"VSMain", L"vs_6_0", L"KUJATA_VERTEX_SHADER", vertexErrors));
	}

	const bool succeeded = pixelShader && (!hasVertexShader || vertexShader);
	if (!succeeded) {
		// 失敗しても、前に成功した版があればそのまま使う(書きかけの保存で絵が消えないように)。
		shader.info.error = errors.empty() ? vertexErrors : errors;
		Logger::Log("[Shader] コンパイル失敗: " + path.generic_string() + "\n" + shader.info.error + "\n");
		return false;
	}

	// 入れ替える前に、古いPSOを使っている描画が終わるのを待つ(初めてのコンパイルなら待たない)。
	if (shader.pixelShader) {
		DirectXCommon::GetInstance()->WaitForGpu();
	}
	for (auto& byType : shader.pipelineStates) {
		for (auto& pipelineState : byType) {
			pipelineState.Reset();
		}
	}
	shader.pixelShader = pixelShader;
	shader.vertexShader = vertexShader;
	shader.info.usable = true;
	shader.info.hasVertexShader = hasVertexShader;
	// 警告だけのときは文面を残しておく(描けるが、直したほうがよいもの)。
	shader.info.error = errors.empty() ? vertexErrors : errors;
	Logger::Log("[Shader] コンパイルしました: " + path.generic_string() + "\n");
	return true;
}

bool GraphicsPipeline::SetCustomCommandList(uint32_t shaderId, PipelineType pipelineType, BlendMode blendMode) {
	if (shaderId == 0 || shaderId > customShaders_.size() || !IsObject3dPipelineType(pipelineType)) {
		return false;
	}
	CustomShader& shader = customShaders_[shaderId - 1];
	if (!shader.info.usable) {
		return false;
	}

	Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState = shader.pipelineStates[static_cast<int32_t>(pipelineType)][static_cast<int32_t>(blendMode)];
	if (!pipelineState) {
		IDxcBlob* vertexBlob = shader.vertexShader ? shader.vertexShader.Get() : defaultVertexShader_.Get();
		const D3D12_SHADER_BYTECODE vertexShader = {vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize()};
		const D3D12_SHADER_BYTECODE pixelShader = {shader.pixelShader->GetBufferPointer(), shader.pixelShader->GetBufferSize()};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeObject3dPipelineDesc(pipelineType, blendMode, vertexShader, pixelShader);
		HRESULT hr = DirectXCommon::GetInstance()->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
		if (FAILED(hr)) {
			// 出力の形(SV_TARGET の数など)が合わないと、コンパイルは通ってもここで失敗する。
			shader.info.usable = false;
			shader.info.error = "PSOを作れません(PSMain の出力が PixelShaderOutput になっているか確かめてください)";
			Logger::Log("[Shader] 失敗: " + shader.info.path.generic_string() + ": " + shader.info.error + "\n");
			return false;
		}
	}

	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_[static_cast<int32_t>(pipelineType)].Get());
	commandList->SetPipelineState(pipelineState.Get());
	return true;
}

bool GraphicsPipeline::ReloadChangedCustomShaders(bool force) {
	bool reloaded = false;
	for (CustomShader& shader : customShaders_) {
		std::error_code errorCode;
		if (!std::filesystem::exists(shader.info.path, errorCode)) {
			continue;
		}
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(shader.info.path, errorCode);
		if (errorCode || (!force && writeTime == shader.lastWriteTime)) {
			continue;
		}
		shader.lastWriteTime = writeTime;
		CompileCustomShader(shader);
		reloaded = true;
	}
	return reloaded;
}

std::vector<CustomShaderInfo> GraphicsPipeline::GetCustomShaderInfos() const {
	std::vector<CustomShaderInfo> infos;
	infos.reserve(customShaders_.size());
	for (const CustomShader& shader : customShaders_) {
		infos.push_back(shader.info);
	}
	return infos;
}

const CustomShaderInfo* GraphicsPipeline::FindCustomShaderInfo(uint32_t shaderId) const {
	if (shaderId == 0 || shaderId > customShaders_.size()) {
		return nullptr;
	}
	return &customShaders_[shaderId - 1].info;
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
