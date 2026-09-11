#include "ParticleModel.h"
#include "../base/DirectXCommon.h"
#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"
#include "../3d/DirectionalLight.h"
#include "../3d/GraphicsPipeline.h"
#include "../3d/ModelUtil.h"
#include <numbers>


namespace KujataEngine {

ParticleModel::~ParticleModel() { instanceParticles_.clear(); }

void ParticleModel::Initialize() {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	// Instancing用のTransformationMatrixリソースを作る
	instancingResource_ = dxCommon->CreateBufferResource(sizeof(ParticleForGPU) * kMaxInstance);

	// 書き込むためのアドレスを取得
	instancingResource_->Map(0, nullptr, (void**)&instancingData_);

	// Instancing用SRVを作成
	ID3D12DescriptorHeap* srvHeap = dxCommon->GetSrvDescriptorHeap();
	const UINT descriptorSizeSRV = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	instancingSrvIndex_ = sInstancingSrvIndexCounter_++;
	instancingSrvHandleCPU_ = srvHeap->GetCPUDescriptorHandleForHeapStart();
	instancingSrvHandleCPU_.ptr += descriptorSizeSRV * instancingSrvIndex_;
	instancingSrvHandleGPU_ = srvHeap->GetGPUDescriptorHandleForHeapStart();
	instancingSrvHandleGPU_.ptr += descriptorSizeSRV * instancingSrvIndex_;

	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

	dxCommon->GetDevice()->CreateShaderResourceView(instancingResource_.Get(), &instancingSrvDesc, instancingSrvHandleCPU_);
		
	// 毎フレーム push_back するため、先に最大数まで予約して再確保スパイクを防ぐ
	instanceParticles_.reserve(kMaxInstance);

	// 単位行列を書き込んでおく
	for (uint32_t i = 0; i < kMaxInstance; ++i) {
		instancingData_[i].WVP = MakeIdentity();
		instancingData_[i].World = MakeIdentity();
	}
}

ParticleModel* ParticleModel::CreateFromOBJ(const std::string& objname, bool enableLighting) {
	ParticleModel* particle = new ParticleModel();
	std::string directoryPathFinal = (GetProjectDataRoot() / "Resources" / objname).generic_string();
	std::string filename = objname + ".obj";

	ModelData rawData = ModelUtil::LoadModelFile(directoryPathFinal, filename);
	rawData.material.enableLighting = enableLighting;
	ModelUtil::ResolveTextureIndex(rawData.material);
	particle->rootLocalMatrix_ = rawData.rootNode.localMatrix;
	particle->CreateVertexBuffer(rawData.vertices);
	particle->CreateMaterialBuffer(rawData.material);
	particle->Initialize();
	return particle;
}

ParticleModel* ParticleModel::CreateCube(const std::string& textureFilePath, bool enableLighting) {
	ParticleModel* particle = new ParticleModel();

	std::vector<VertexData> vertices = {
	    // 前面 (Z+)
	    {{-0.5f, 0.5f, 0.5f, 1.0f},   {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
	    {{-0.5f, -0.5f, 0.5f, 1.0f},  {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },
	    {{0.5f, -0.5f, 0.5f, 1.0f},   {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },
	    {{-0.5f, 0.5f, 0.5f, 1.0f},   {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
	    {{0.5f, -0.5f, 0.5f, 1.0f},   {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },
	    {{0.5f, 0.5f, 0.5f, 1.0f},    {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },

	    // 背面 (Z-)
	    {{0.5f, 0.5f, -0.5f, 1.0f},   {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	    {{0.5f, -0.5f, -0.5f, 1.0f},  {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
	    {{-0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
	    {{0.5f, 0.5f, -0.5f, 1.0f},   {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	    {{-0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
	    {{-0.5f, 0.5f, -0.5f, 1.0f},  {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},

	    // 上面 (Y+)
	    {{-0.5f, 0.5f, -0.5f, 1.0f},  {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
	    {{-0.5f, 0.5f, 0.5f, 1.0f},   {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f} },
	    {{0.5f, 0.5f, 0.5f, 1.0f},    {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f} },
	    {{-0.5f, 0.5f, -0.5f, 1.0f},  {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
	    {{0.5f, 0.5f, 0.5f, 1.0f},    {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f} },
	    {{0.5f, 0.5f, -0.5f, 1.0f},   {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },

	    // 下面 (Y-)
	    {{-0.5f, -0.5f, 0.5f, 1.0f},  {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
	    {{-0.5f, -0.5f, -0.5f, 1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
	    {{0.5f, -0.5f, -0.5f, 1.0f},  {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
	    {{-0.5f, -0.5f, 0.5f, 1.0f},  {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
	    {{0.5f, -0.5f, -0.5f, 1.0f},  {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
	    {{0.5f, -0.5f, 0.5f, 1.0f},   {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},

	    // 右面 (X+)
	    {{0.5f, 0.5f, 0.5f, 1.0f},    {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
	    {{0.5f, -0.5f, 0.5f, 1.0f},   {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
	    {{0.5f, -0.5f, -0.5f, 1.0f},  {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
	    {{0.5f, 0.5f, 0.5f, 1.0f},    {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
	    {{0.5f, -0.5f, -0.5f, 1.0f},  {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
	    {{0.5f, 0.5f, -0.5f, 1.0f},   {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },

	    // 左面 (X-)
	    {{-0.5f, 0.5f, -0.5f, 1.0f},  {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
	    {{-0.5f, -0.5f, -0.5f, 1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
	    {{-0.5f, -0.5f, 0.5f, 1.0f},  {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
	    {{-0.5f, 0.5f, -0.5f, 1.0f},  {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
	    {{-0.5f, -0.5f, 0.5f, 1.0f},  {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
	    {{-0.5f, 0.5f, 0.5f, 1.0f},   {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
	};

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(enableLighting));

	particle->CreateVertexBuffer(vertices);
	particle->CreateMaterialBuffer(defaultMaterial);
	particle->Initialize();
	return particle;
}

ParticleModel* ParticleModel::CreatePlane(const std::string& textureFilePath, bool enableLighting) {
	ParticleModel* particle = new ParticleModel();

	std::vector<VertexData> vertices;

	vertices.push_back({
	    .position = {1.0f, 1.0f, 0.0f, 1.0f},
          .texcoord = {0.0f, 0.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 左上
	vertices.push_back({
	    .position = {-1.0f, 1.0f, 0.0f, 1.0f},
          .texcoord = {1.0f, 0.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 右上
	vertices.push_back({
	    .position = {1.0f, -1.0f, 0.0f, 1.0f},
          .texcoord = {0.0f, 1.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 左下
	vertices.push_back({
	    .position = {1.0f, -1.0f, 0.0f, 1.0f},
          .texcoord = {0.0f, 1.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 左下
	vertices.push_back({
	    .position = {-1.0f, 1.0f, 0.0f, 1.0f},
          .texcoord = {1.0f, 0.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 右上
	vertices.push_back({
	    .position = {-1.0f, -1.0f, 0.0f, 1.0f},
          .texcoord = {1.0f, 1.0f},
          .normal = {0.0f, 0.0f, 1.0f}
    }); // 右下

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(enableLighting));

	particle->CreateVertexBuffer(vertices);
	particle->CreateMaterialBuffer(defaultMaterial);
	particle->Initialize();
	return particle;
}

ParticleModel* ParticleModel::CreateTriangle(const std::string& textureFilePath, bool enableLighting) {
	ParticleModel* particle = new ParticleModel();

	std::vector<VertexData> vertices;

	const float halfWidth = std::numbers::sqrt3_v<float> * 0.5f;

	vertices.push_back({
	    .position = {0.0f, 1.0f, 0.0f, 1.0f},
	    .texcoord = {0.5f, 0.0f},
	    .normal = {0.0f, 0.0f, 1.0f}
	}); // 上

	vertices.push_back({
	    .position = {-halfWidth, -0.5f, 0.0f, 1.0f},
	    .texcoord = {0.0f, 1.0f},
	    .normal = {0.0f, 0.0f, 1.0f}
	}); // 左下

	vertices.push_back({
	    .position = {halfWidth, -0.5f, 0.0f, 1.0f},
	    .texcoord = {1.0f, 1.0f},
	    .normal = {0.0f, 0.0f, 1.0f}
	}); // 右下

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(enableLighting));

	particle->CreateVertexBuffer(vertices);
	particle->CreateMaterialBuffer(defaultMaterial);
	particle->Initialize();
	return particle;
}

ParticleModel* ParticleModel::CreateTetrahedron(const std::string& textureFilePath, bool enableLighting) {
	ParticleModel* particle = new ParticleModel();

	std::vector<VertexData> vertices;

	const Vector3 v0 = { 1.0f,  1.0f,  1.0f };
	const Vector3 v1 = { -1.0f, -1.0f,  1.0f };
	const Vector3 v2 = { -1.0f,  1.0f, -1.0f };
	const Vector3 v3 = { 1.0f, -1.0f, -1.0f };

	auto AddFace = [&](const Vector3& a, const Vector3& b, const Vector3& c) {

		Vector3 normal =
			Normalize(Cross(b - a, c - a));

		vertices.push_back({
			.position = {a.x, a.y, a.z, 1.0f},
			.texcoord = {0.5f, 0.0f},
			.normal = normal
			});

		vertices.push_back({
			.position = {b.x, b.y, b.z, 1.0f},
			.texcoord = {0.0f, 1.0f},
			.normal = normal
			});

		vertices.push_back({
			.position = {c.x, c.y, c.z, 1.0f},
			.texcoord = {1.0f, 1.0f},
			.normal = normal
			});
		};

	// 4面
	AddFace(v0, v2, v1);
	AddFace(v0, v1, v3);
	AddFace(v0, v3, v2);
	AddFace(v1, v2, v3);

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(enableLighting));

	particle->CreateVertexBuffer(vertices);
	particle->CreateMaterialBuffer(defaultMaterial);
	particle->Initialize();

	return particle;
}

void ParticleModel::PreDraw() {
	ModelUtil::SetCommonRenderState();
}

void ParticleModel::PostDraw() {
	// 将来的にここで描画状態のリセットなどを行う
}

void ParticleModel::Draw() {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// RootSignature と PSO をセット
	GraphicsPipeline::GetInstance()->SetCommandList(PipelineType::kParticle, blendMode_);

	// VBVを設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// マテリアルCBuffer（RootParameter[0]: PixelShader, b0）
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// Instancing SRV（RootParameter[1]: VS t0 StructuredBuffer）
	commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU_);

	// テクスチャSRV（RootParameter[2]: DescriptorTable）
	auto handle = TextureManager::GetInstance()->GetSrvHandle(textureIndex_);
	commandList->SetGraphicsRootDescriptorTable(2, handle);

	// ライト
	commandList->SetGraphicsRootConstantBufferView(3, DirectionalLight::GetInstance()->GetResource()->GetGPUVirtualAddress());
	// 描画
	commandList->DrawInstanced(vertexCount_, static_cast<uint32_t>(instanceParticles_.size()), 0, 0);
	instanceParticles_.clear();
}

void ParticleModel::UpdateBuffer() { memcpy(instancingData_, instanceParticles_.data(), sizeof(ParticleForGPU) * instanceParticles_.size()); }

void ParticleModel::CreateVertexBuffer(const std::vector<VertexData>& vertices) {
	vertexCount_ = static_cast<uint32_t>(vertices.size());
	vertexResource_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(VertexData) * vertexCount_);

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertexCount_);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	VertexData* vertexMap = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap));
	std::memcpy(vertexMap, vertices.data(), sizeof(VertexData) * vertexCount_);
	vertexResource_->Unmap(0, nullptr);
}

void ParticleModel::CreateMaterialBuffer(const MaterialData& material) {
	materialResource_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(MaterialData));

	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialMap_));
	materialMap_->color = material.color;
	materialMap_->enableLighting = material.enableLighting;
	materialMap_->uvTransform = MakeIdentity();
	textureIndex_ = material.textureIndex;
}
} // namespace KujataEngine
