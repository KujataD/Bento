#include "Model.h"
#include "../base/DirectXCommon.h"
#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"
#include "DirectionalLight.h"
#include "GraphicsPipeline.h"
#include "ModelUtil.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "../shadow/ShadowMap.h"
#include "../shadow/ShadowPipeline.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace KujataEngine {

Model* Model::CreateFromOBJ(const std::string& objname, ShaderModel shaderModel) {
	Model* model = new Model();
	std::string resourcesRoot = (GetProjectDataRoot() / "Resources").generic_string();
	std::string directoryPathFinal = resourcesRoot + "/" + objname;
	std::string filename = objname + ".obj";

	// 規約は Resources/<name>/<name>.obj。存在しなければ Resources 直下(Resources/<name>.obj)を試す。
	if (!std::filesystem::exists(directoryPathFinal + "/" + filename)) {
		directoryPathFinal = resourcesRoot;
	}

	ModelData rawData = ModelUtil::LoadModelFile(directoryPathFinal, filename);
	model->rootLocalMatrix_ = rawData.rootNode.localMatrix;
	model->BuildSubMeshes(rawData, shaderModel);
	return model;
}

void Model::SetUVTransform(const Vector2& offset, const Vector2& scale, float rotation) {
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix({scale.x, scale.y, 1.0f});
	uvTransformMatrix = uvTransformMatrix * MakeRotateZMatrix(rotation);
	uvTransformMatrix = uvTransformMatrix * MakeTranslateMatrix({offset.x, offset.y, 0.0f});
	for (SubMesh& subMesh : subMeshes_) {
		if (subMesh.materialMap) {
			subMesh.materialMap->uvTransform = uvTransformMatrix;
		}
	}
}

void Model::SetSubMeshUVTransform(size_t index, const Vector2& offset, const Vector2& scale, float rotation) {
	if (index >= subMeshes_.size() || !subMeshes_[index].materialMap) {
		return;
	}
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix({scale.x, scale.y, 1.0f});
	uvTransformMatrix = uvTransformMatrix * MakeRotateZMatrix(rotation);
	uvTransformMatrix = uvTransformMatrix * MakeTranslateMatrix({offset.x, offset.y, 0.0f});
	subMeshes_[index].materialMap->uvTransform = uvTransformMatrix;
}

Model* Model::CreateFromGlTF(const std::string& objname, ShaderModel shaderModel) {
	Model* model = new Model();
	std::string directoryPathFinal = (GetProjectDataRoot() / "Resources" / objname).generic_string();
	std::string filename = objname + ".gltf";

	ModelData rawData = ModelUtil::LoadModelFile(directoryPathFinal, filename);
	model->rootLocalMatrix_ = rawData.rootNode.localMatrix;
	model->BuildSubMeshes(rawData, shaderModel);
	return model;
}

Model* Model::TryCreateFromFile(const std::string& filePath, ShaderModel shaderModel) {
	std::filesystem::path path(filePath);
	std::filesystem::path directory = path.parent_path();
	std::string filename = path.filename().string();

	ModelData rawData{};
	if (!ModelUtil::TryLoadModelFile(directory.generic_string(), filename, rawData)) {
		return nullptr;
	}

	Model* model = new Model();
	model->rootLocalMatrix_ = rawData.rootNode.localMatrix;
	model->BuildSubMeshes(rawData, shaderModel);
	return model;
}

Model* Model::CreateSphere(const std::string& textureFilePath, ShaderModel shaderModel, uint32_t subdivision) {
	Model* model = new Model();
	const float kLonEvery = static_cast<float>(2.0f * std::numbers::pi_v<float> / subdivision);
	const float kLatEvery = static_cast<float>(std::numbers::pi_v<float> / subdivision);

	std::vector<VertexData> vertices;
	vertices.resize(6 * subdivision * subdivision);

	for (uint32_t latIndex = 0; latIndex < subdivision; ++latIndex) {
		float lat = static_cast<float>(-std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex);

		for (uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex) {
			float lon = lonIndex * kLonEvery;

			float u0 = float(lonIndex) / float(subdivision);
			float u1 = float(lonIndex + 1) / float(subdivision);

			float v0 = 1.0f - float(latIndex) / float(subdivision);
			float v1 = 1.0f - float(latIndex + 1) / float(subdivision);

			uint32_t startIndex = (latIndex * subdivision + lonIndex) * 6;

			vertices[startIndex].position = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon), 1.0f };
			vertices[startIndex].texcoord = { u0, v0 };
			vertices[startIndex].normal = { vertices[startIndex].position.x, vertices[startIndex].position.y, vertices[startIndex].position.z };

			vertices[startIndex + 1].position = { cosf(lat + kLatEvery) * cosf(lon), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon), 1.0f };
			vertices[startIndex + 1].texcoord = { u0, v1 };
			vertices[startIndex + 1].normal = { vertices[startIndex + 1].position.x, vertices[startIndex + 1].position.y, vertices[startIndex + 1].position.z };

			vertices[startIndex + 2].position = { cosf(lat) * cosf(lon + kLonEvery), sinf(lat), cosf(lat) * sinf(lon + kLonEvery), 1.0f };
			vertices[startIndex + 2].texcoord = { u1, v0 };
			vertices[startIndex + 2].normal = { vertices[startIndex + 2].position.x, vertices[startIndex + 2].position.y, vertices[startIndex + 2].position.z };

			vertices[startIndex + 3].position = { cosf(lat) * cosf(lon + kLonEvery), sinf(lat), cosf(lat) * sinf(lon + kLonEvery), 1.0f };
			vertices[startIndex + 3].texcoord = { u1, v0 };
			vertices[startIndex + 3].normal = { vertices[startIndex + 3].position.x, vertices[startIndex + 3].position.y, vertices[startIndex + 3].position.z };

			vertices[startIndex + 4].position = { cosf(lat + kLatEvery) * cosf(lon), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon), 1.0f };
			vertices[startIndex + 4].texcoord = { u0, v1 };
			vertices[startIndex + 4].normal = { vertices[startIndex + 4].position.x, vertices[startIndex + 4].position.y, vertices[startIndex + 4].position.z };

			vertices[startIndex + 5].position = { cosf(lat + kLatEvery) * cosf(lon + kLonEvery), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon + kLonEvery), 1.0f };
			vertices[startIndex + 5].texcoord = { u1, v1 };
			vertices[startIndex + 5].normal = { vertices[startIndex + 5].position.x, vertices[startIndex + 5].position.y, vertices[startIndex + 5].position.z };
		}
	}

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreateCube(const std::string& textureFilePath, ShaderModel shaderModel) {
	Model* model = new Model();

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
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreateCapsule(const std::string& textureFilePath, ShaderModel shaderModel, float radius, float height, uint32_t subdivision) {
	Model* model = new Model();

	const uint32_t slices = (subdivision < 3) ? 3 : subdivision;         // 経度分割
	const uint32_t stacks = (subdivision / 2 < 1) ? 1 : subdivision / 2; // 半球あたりの緯度分割
	const float pi = std::numbers::pi_v<float>;

	// spineの半長 = 全長の半分から半径を引いた分(cylinder部分の半分)。
	float cylinderHalf = height * 0.5f - radius;
	if (cylinderHalf < 0.0f) {
		cylinderHalf = 0.0f;
	}

	struct RingVertex {
		Vector3 position;
		Vector3 normal;
	};

	// 下半球(-pi/2 → 0)→ 上半球(0 → pi/2) の順にリングを積む。
	// 下半球末尾(赤道, y=-cylinderHalf)と上半球先頭(赤道, y=+cylinderHalf)の間がcylinder側面になる。
	std::vector<std::vector<RingVertex>> rings;
	auto buildHemisphere = [&](bool top) {
		float centerY = top ? cylinderHalf : -cylinderHalf;
		for (uint32_t s = 0; s <= stacks; ++s) {
			float ratio = static_cast<float>(s) / static_cast<float>(stacks);
			float phi = top ? (pi * 0.5f) * ratio : (-pi * 0.5f + (pi * 0.5f) * ratio);
			std::vector<RingVertex> ring;
			ring.reserve(slices + 1);
			for (uint32_t i = 0; i <= slices; ++i) {
				float lon = (2.0f * pi) * (static_cast<float>(i) / static_cast<float>(slices));
				Vector3 normal = {cosf(phi) * cosf(lon), sinf(phi), cosf(phi) * sinf(lon)};
				Vector3 position = {radius * normal.x, centerY + radius * normal.y, radius * normal.z};
				ring.push_back({position, normal});
			}
			rings.push_back(std::move(ring));
		}
	};

	buildHemisphere(false);
	buildHemisphere(true);

	std::vector<VertexData> vertices;
	const uint32_t ringCount = static_cast<uint32_t>(rings.size());
	auto pushVertex = [&](const RingVertex& rv, float u, float v) {
		VertexData vertex{};
		vertex.position = {rv.position.x, rv.position.y, rv.position.z, 1.0f};
		vertex.texcoord = {u, v};
		vertex.normal = rv.normal;
		vertices.push_back(vertex);
	};

	for (uint32_t ringIndex = 0; ringIndex + 1 < ringCount; ++ringIndex) {
		const std::vector<RingVertex>& r0 = rings[ringIndex];
		const std::vector<RingVertex>& r1 = rings[ringIndex + 1];
		float v0 = static_cast<float>(ringIndex) / static_cast<float>(ringCount - 1);
		float v1 = static_cast<float>(ringIndex + 1) / static_cast<float>(ringCount - 1);
		for (uint32_t i = 0; i < slices; ++i) {
			float u0 = static_cast<float>(i) / static_cast<float>(slices);
			float u1 = static_cast<float>(i + 1) / static_cast<float>(slices);

			// CreateSphereと同じ巻き順で2三角形を張る。
			pushVertex(r0[i], u0, v0);
			pushVertex(r1[i], u0, v1);
			pushVertex(r0[i + 1], u1, v0);

			pushVertex(r0[i + 1], u1, v0);
			pushVertex(r1[i], u0, v1);
			pushVertex(r1[i + 1], u1, v1);
		}
	}

	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreateDynamic(uint32_t maxVertices, const std::string& textureFilePath, ShaderModel shaderModel) {
	Model* model = new Model();

	if (maxVertices == 0) {
		maxVertices = 3;
	}

	SubMesh subMesh{};
	// 中身はまだ無いが、バッファだけ先に確保する。以後の更新はmemcpyだけで済む。
	subMesh.vertexCount = 0;
	subMesh.vertexCapacity = maxVertices;

	subMesh.vertexResource = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(VertexData) * maxVertices);
	subMesh.vertexBufferView.BufferLocation = subMesh.vertexResource->GetGPUVirtualAddress();
	subMesh.vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * maxVertices);
	subMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);
	// Uploadヒープなので張りっぱなしにしてよい(毎フレームMap/Unmapするより安い)。
	subMesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&subMesh.vertexMap));

	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	subMesh.materialResource = DirectXCommon::GetInstance()->CreateBufferResource((sizeof(MaterialData) + 0xff) & ~0xff);
	subMesh.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&subMesh.materialMap));
	subMesh.materialMap->color = defaultMaterial.color;
	subMesh.materialMap->enableLighting = defaultMaterial.enableLighting;
	subMesh.materialMap->uvTransform = MakeIdentity();
	subMesh.materialMap->shininess = defaultMaterial.shininess;
	subMesh.materialMap->emissiveColor = defaultMaterial.emissiveColor;
	subMesh.materialMap->emissiveIntensity = defaultMaterial.emissiveIntensity;
	subMesh.materialMap->emissiveEnabled = defaultMaterial.emissiveEnabled;
	subMesh.materialMap->bloomIntensity = defaultMaterial.bloomIntensity;
	subMesh.materialMap->bloomThreshold = defaultMaterial.bloomThreshold;
	subMesh.materialMap->bloomSoftKnee = defaultMaterial.bloomSoftKnee;
	subMesh.textureIndex = defaultMaterial.textureIndex;

	model->subMeshes_.push_back(std::move(subMesh));
	return model;
}

void Model::UpdateDynamicVertices(const std::vector<VertexData>& vertices) {
	if (subMeshes_.empty()) {
		return;
	}
	SubMesh& subMesh = subMeshes_[0];
	if (!subMesh.vertexMap) {
		return;
	}

	uint32_t count = static_cast<uint32_t>(vertices.size());
	if (count > subMesh.vertexCapacity) {
		count = subMesh.vertexCapacity;
	}
	if (count > 0) {
		std::memcpy(subMesh.vertexMap, vertices.data(), sizeof(VertexData) * count);
	}
	// 描画数だけを変える。バッファそのものは作り直さない。
	subMesh.vertexCount = count;
}

Model* Model::CreateRing(const std::string& textureFilePath, ShaderModel shaderModel, uint32_t subdivision, float innerRatio) {
	Model* model = new Model();

	if (subdivision < 3) {
		subdivision = 3;
	}
	innerRatio = std::clamp(innerRatio, 0.0f, 0.99f);

	std::vector<VertexData> vertices;
	vertices.reserve(static_cast<size_t>(subdivision) * 6);

	const float kAngleEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(subdivision);

	// 内周と外周の対応する点で四角形を作り、それを2つの三角形に割る。
	// 上(+Y)から見て時計回りに並べる = 背面カリングで上から見えるようにする。
	for (uint32_t index = 0; index < subdivision; ++index) {
		float angle0 = kAngleEvery * static_cast<float>(index);
		float angle1 = kAngleEvery * static_cast<float>(index + 1);

		float sin0 = std::sin(angle0);
		float cos0 = std::cos(angle0);
		float sin1 = std::sin(angle1);
		float cos1 = std::cos(angle1);

		float u0 = static_cast<float>(index) / static_cast<float>(subdivision);
		float u1 = static_cast<float>(index + 1) / static_cast<float>(subdivision);

		// v=0が内周、v=1が外周。帯の内→外のグラデーションをシェーダーで作れるようにする。
		VertexData inner0{.position = {sin0 * innerRatio, 0.0f, cos0 * innerRatio, 1.0f}, .texcoord = {u0, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}};
		VertexData outer0{.position = {sin0, 0.0f, cos0, 1.0f}, .texcoord = {u0, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}};
		VertexData inner1{.position = {sin1 * innerRatio, 0.0f, cos1 * innerRatio, 1.0f}, .texcoord = {u1, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}};
		VertexData outer1{.position = {sin1, 0.0f, cos1, 1.0f}, .texcoord = {u1, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}};

		vertices.push_back(inner0);
		vertices.push_back(outer0);
		vertices.push_back(inner1);

		vertices.push_back(inner1);
		vertices.push_back(outer0);
		vertices.push_back(outer1);
	}

	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreatePlane(const std::string& textureFilePath, ShaderModel shaderModel) {
	Model* model = new Model();

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
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreateTriangle(const std::string& textureFilePath, ShaderModel shaderModel) {
	Model* model = new Model();

	std::vector<VertexData> vertices;

	const float halfWidth = std::numbers::sqrt3_v<float> *0.5f;

	vertices.push_back({
		.position = {0.0f, 1.0f, 0.0f, 1.0f},
		.texcoord = {0.5f, 0.0f},
		.normal = {0.0f, 0.0f, 1.0f}
		}); // 上

	vertices.push_back({
		.position = {halfWidth, -0.5f, 0.0f, 1.0f},
		.texcoord = {1.0f, 1.0f},
		.normal = {0.0f, 0.0f, 1.0f}
		}); // 右下

	vertices.push_back({
		.position = {-halfWidth, -0.5f, 0.0f, 1.0f},
		.texcoord = {0.0f, 1.0f},
		.normal = {0.0f, 0.0f, 1.0f}
		}); // 左下

	// MaterialData
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}

Model* Model::CreateTetrahedron(const std::string& textureFilePath, ShaderModel shaderModel) {
	Model* model = new Model();

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
	MaterialData defaultMaterial = ModelUtil::CreateTexturedMaterial(textureFilePath, static_cast<int32_t>(shaderModel));

	model->AddSubMesh(vertices, defaultMaterial);

	return model;
}
void Model::PreDraw() {
	ModelUtil::SetCommonRenderState();
}

void Model::PostDraw() {
	// 将来的にここで描画状態のリセットなどを行う
}

void Model::Draw(const WorldTransform& worldTransform, const Camera& camera, FillMode fillMode) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// RootNodeのローカル行列をモデル固有の補正として描画時だけ適用する
	Matrix4x4 modelWorldMatrix = rootLocalMatrix_ * worldTransform.matWorld_;
	worldTransform.TransferMatrix(camera, modelWorldMatrix);

	// RootSignature と PSO をセット
	if (fillMode == kFillModeSolid) {
		// 背面カリングと深度書き込みの組み合わせでPSOを選ぶ。
		// 半透明/加算が深度を書くと、後から描く背後の物を隠し、自分の裏面とも喧嘩する。
		PipelineType pipelineType = PipelineType::kObject3d;
		if (doubleSided_) {
			pipelineType = depthWrite_ ? PipelineType::kObject3dDoubleSided : PipelineType::kObject3dDoubleSidedNoDepthWrite;
		} else if (!depthWrite_) {
			pipelineType = PipelineType::kObject3dNoDepthWrite;
		}
		GraphicsPipeline::GetInstance()->SetCommandList(pipelineType, blendMode_);
	}
	else if (fillMode == kFillModeWireframe) {
		GraphicsPipeline::GetInstance()->SetCommandList(PipelineType::kObject3dWireframe, blendMode_);
	}

	// 全サブメッシュ共通のCBufferは1回だけセットする。
	// WVP・WorldCBuffer（RootParameter[1]: VertexShader, b0）
	commandList->SetGraphicsRootConstantBufferView(1, worldTransform.GetConstBuffer()->GetGPUVirtualAddress());
	// directional light
	commandList->SetGraphicsRootConstantBufferView(3, DirectionalLight::GetInstance()->GetResource()->GetGPUVirtualAddress());
	// カメラ（RootParameter[4]: VertexShader, b2）
	commandList->SetGraphicsRootConstantBufferView(4, camera.GetCameraForGPUResource()->GetGPUVirtualAddress());
	// pointlight / spotlight
	commandList->SetGraphicsRootConstantBufferView(5, PointLight::GetInstance()->GetResource()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(6, SpotLight::GetInstance()->GetResource()->GetGPUVirtualAddress());
	// シャドウマップ(t1)とライト行列(b5)。未初期化時はバインドせず、PS側はshadow=1(影なし)で動く。
	ShadowMap* shadowMap = ShadowMap::GetInstance();
	if (shadowMap->IsInitialized()) {
		commandList->SetGraphicsRootDescriptorTable(7, shadowMap->GetSrvHandleGPU());
		commandList->SetGraphicsRootConstantBufferView(8, shadowMap->GetConstBuffer()->GetGPUVirtualAddress());
	}

	// サブメッシュごとに 頂点バッファ・マテリアル・テクスチャ を切り替えて描画する。
	for (const SubMesh& subMesh : subMeshes_) {
		// 動的メッシュは中身が空になることがある(トレイルの点がまだ無い等)。
		if (subMesh.vertexCount == 0) {
			continue;
		}
		commandList->IASetVertexBuffers(0, 1, &subMesh.vertexBufferView);
		// マテリアルCBuffer（RootParameter[0]: PixelShader, b0）
		commandList->SetGraphicsRootConstantBufferView(0, subMesh.materialResource->GetGPUVirtualAddress());
		// テクスチャSRV（RootParameter[2]: DescriptorTable）
		auto handle = TextureManager::GetInstance()->GetSrvHandle(subMesh.textureIndex);
		commandList->SetGraphicsRootDescriptorTable(2, handle);
		// エミッションマップ(RootParameter[9]: t2)。未指定(0)は白へ倒して「マップ無し=1倍」にする。
		uint32_t emissiveIndex = subMesh.emissiveTextureIndex;
		if (emissiveIndex == 0) {
			emissiveIndex = TextureManager::GetInstance()->GetDefaultWhiteTexture();
		}
		commandList->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandle(emissiveIndex));
		commandList->DrawInstanced(subMesh.vertexCount, 1, 0, 0);
	}
}

void Model::DrawShadow(const WorldTransform& worldTransform, const Matrix4x4& lightViewProjection) {
	ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

	// Drawと同じくRootNodeのローカル行列を掛けてから、ライト視点のWVPをシャドウ枠へ転送する。
	Matrix4x4 modelWorldMatrix = rootLocalMatrix_ * worldTransform.matWorld_;
	worldTransform.TransferMatrixWithViewProjection(lightViewProjection, modelWorldMatrix);

	ShadowPipeline::GetInstance()->SetCommandList();
	commandList->SetGraphicsRootConstantBufferView(ShadowPipeline::kRootParamTransform, worldTransform.GetConstBuffer()->GetGPUVirtualAddress());

	// マテリアルもテクスチャも要らないので、頂点バッファだけ差し替えて描く。
	for (const SubMesh& subMesh : subMeshes_) {
		// 動的メッシュは中身が空になることがある(トレイルの点がまだ無い等)。
		if (subMesh.vertexCount == 0) {
			continue;
		}
		commandList->IASetVertexBuffers(0, 1, &subMesh.vertexBufferView);
		commandList->DrawInstanced(subMesh.vertexCount, 1, 0, 0);
	}
}

void Model::AddSubMesh(const std::vector<VertexData>& vertices, const MaterialData& material) {
	// 空メッシュは0サイズバッファ生成を避けるためスキップする(読み込み失敗時の保険)。
	if (vertices.empty()) {
		return;
	}

	SubMesh subMesh{};
	subMesh.vertexCount = static_cast<uint32_t>(vertices.size());

	// 頂点バッファ生成
	subMesh.vertexResource = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(VertexData) * subMesh.vertexCount);
	subMesh.vertexBufferView.BufferLocation = subMesh.vertexResource->GetGPUVirtualAddress();
	subMesh.vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * subMesh.vertexCount);
	subMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexMap = nullptr;
	subMesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap));
	std::memcpy(vertexMap, vertices.data(), sizeof(VertexData) * subMesh.vertexCount);
	subMesh.vertexResource->Unmap(0, nullptr);

	// マテリアルCBuffer生成
	subMesh.materialResource = DirectXCommon::GetInstance()->CreateBufferResource((sizeof(MaterialData) + 0xff) & ~0xff);
	subMesh.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&subMesh.materialMap));
	subMesh.materialMap->color = material.color;
	subMesh.materialMap->enableLighting = material.enableLighting;
	subMesh.materialMap->uvTransform = MakeIdentity();
	subMesh.materialMap->shininess = material.shininess;
	// Map先はUploadヒープの生メモリなのでCB転送部は必ず明示的に初期化する。
	subMesh.materialMap->emissiveColor = material.emissiveColor;
	subMesh.materialMap->emissiveIntensity = material.emissiveIntensity;
	subMesh.materialMap->emissiveEnabled = material.emissiveEnabled;
	subMesh.materialMap->bloomIntensity = material.bloomIntensity;
	subMesh.materialMap->bloomThreshold = material.bloomThreshold;
	subMesh.materialMap->bloomSoftKnee = material.bloomSoftKnee;
	subMesh.textureIndex = material.textureIndex;

	// raycast/preview用の統合頂点へ追記する。
	mergedVertices_.insert(mergedVertices_.end(), vertices.begin(), vertices.end());

	subMeshes_.push_back(std::move(subMesh));
}

void Model::BuildSubMeshes(ModelData& modelData, ShaderModel shaderModel) {
	if (!modelData.meshes.empty()) {
		// サブメッシュ別(MultiMesh)。各パーツのマテリアル/テクスチャを個別に解決する。
		for (MeshData& mesh : modelData.meshes) {
			mesh.material.enableLighting = static_cast<int32_t>(shaderModel);
			ModelUtil::ResolveTextureIndex(mesh.material);
			AddSubMesh(mesh.vertices, mesh.material);
		}
		return;
	}

	// フォールバック: サブメッシュ情報が無い場合は統合頂点/代表マテリアルで1サブメッシュ。
	modelData.material.enableLighting = static_cast<int32_t>(shaderModel);
	ModelUtil::ResolveTextureIndex(modelData.material);
	AddSubMesh(modelData.vertices, modelData.material);
}
} // namespace KujataEngine
