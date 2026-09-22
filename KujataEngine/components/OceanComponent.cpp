#include "OceanComponent.h"

#include "../3d/Camera.h"
#include "../assets/MaterialAsset.h"
#include "../base/DirectXCommon.h"
#include "../base/Time.h"
#include "../math/MathUtil.h"
#include "../runtime/AssetResolver.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace KujataEngine {

namespace {

// マテリアルの Shader が空のときに使う、エンジン同梱の海のシェーダー。
constexpr const char* kOceanShaderPath = "engine:Custom/Ocean.hlsl";

} // namespace

void OceanComponent::OnPlayStart() {
	// Play のたびに波を同じところから始める(リプレイで同じ海になるように)。
	waveTime_ = 0.0f;
	playing_ = true;
}

void OceanComponent::OnPlayStop() { playing_ = false; }

void OceanComponent::Update() {
	// ゲームの時間で進める(ヒットストップ・スローで波も止まる。浮かぶ物の判定と見た目がずれない)。
	waveTime_ += Time::GetDeltaTime() * timeScale_;
}

float OceanComponent::EvaluateWaves(const Vector2& worldXZ) const {
	// Ocean.hlsl の WaveHeight と同じ式。
	float height = 0.0f;
	for (const Vector4& wave : waves_) {
		if (wave.y <= 0.0f) {
			continue;
		}
		const float angle = wave.x * std::numbers::pi_v<float> / 180.0f;
		const float k = 2.0f * std::numbers::pi_v<float> / wave.y;
		const float along = std::cos(angle) * worldXZ.x + std::sin(angle) * worldXZ.y;
		height += wave.z * std::sin(k * (along - wave.w * waveTime_));
	}
	return height;
}

void OceanComponent::GetGridDivisions(uint32_t& outX, uint32_t& outZ) const {
	// 長い方の辺を Divisions マスにし、短い方はマスがなるべく正方形になるように決める。
	const int divisions = std::clamp(divisions_, 1, 512);
	const float longSide = (std::max)((std::max)(sizeX_, sizeZ_), 0.1f);
	const float cell = longSide / static_cast<float>(divisions);
	outX = static_cast<uint32_t>(std::clamp(static_cast<int>(std::round((std::max)(sizeX_, 0.1f) / cell)), 1, 512));
	outZ = static_cast<uint32_t>(std::clamp(static_cast<int>(std::round((std::max)(sizeZ_, 0.1f) / cell)), 1, 512));
}

bool OceanComponent::GetSurfaceHeight(float x, float z, float& outHeight) const {
	const GameObject* owner = GetOwner();
	if (!owner || !IsEnabled()) {
		return false;
	}
	// この海の板の上での位置(ローカル座標)にする。格子は原点が中心で、XZ に -size/2 〜 +size/2。
	const Matrix4x4& world = owner->GetTransform().matWorld_;
	const Vector3 local = Transform(Vector3{x, 0.0f, z}, Inverse(world));
	const float halfX = sizeX_ * 0.5f;
	const float halfZ = sizeZ_ * 0.5f;
	if (local.x < -halfX || local.x > halfX || local.z < -halfZ || local.z > halfZ) {
		return false;
	}

	// 描かれている三角形の上の高さを返す(頂点だけが波の式どおりで、間は平らな三角形なので、式をそのまま使うと
	// マスが粗いときに見た目とずれる)。格子の作り方(Model::CreateGrid)と同じ三角形の分け方にする。
	uint32_t divisionsX = 0;
	uint32_t divisionsZ = 0;
	GetGridDivisions(divisionsX, divisionsZ);
	const float cellX = sizeX_ / static_cast<float>(divisionsX);
	const float cellZ = sizeZ_ / static_cast<float>(divisionsZ);
	const float gx = (local.x + halfX) / cellX;
	const float gz = (local.z + halfZ) / cellZ;
	const float ix = std::clamp(std::floor(gx), 0.0f, static_cast<float>(divisionsX - 1));
	const float iz = std::clamp(std::floor(gz), 0.0f, static_cast<float>(divisionsZ - 1));
	const float fx = gx - ix;
	const float fz = gz - iz;

	// 格子の頂点(ローカル)の、ワールドでの高さ = 板の高さ + 波(頂点シェーダーはローカルの y に足すので、Y の拡大が掛かる)。
	const float scaleY = Length(Vector3{world.m[1][0], world.m[1][1], world.m[1][2]});
	auto vertexHeight = [&](float cx, float cz) {
		const Vector3 p = Transform(Vector3{-halfX + cx * cellX, 0.0f, -halfZ + cz * cellZ}, world);
		return p.y + EvaluateWaves({p.x, p.z}) * scaleY;
	};
	// 1マスの三角形は (x0,z0)(x0,z1)(x1,z0) と (x1,z0)(x0,z1)(x1,z1)。対角線は fx + fz = 1。
	if (fx + fz <= 1.0f) {
		const float h00 = vertexHeight(ix, iz);
		const float h10 = vertexHeight(ix + 1.0f, iz);
		const float h01 = vertexHeight(ix, iz + 1.0f);
		outHeight = h00 + (h10 - h00) * fx + (h01 - h00) * fz;
	} else {
		const float h11 = vertexHeight(ix + 1.0f, iz + 1.0f);
		const float h10 = vertexHeight(ix + 1.0f, iz);
		const float h01 = vertexHeight(ix, iz + 1.0f);
		outHeight = h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
	}
	return true;
}

bool OceanComponent::TryGetSurfaceHeight(const Scene& scene, float x, float z, float& outHeight) {
	bool found = false;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			const OceanComponent* ocean = dynamic_cast<const OceanComponent*>(component.get());
			float height = 0.0f;
			if (ocean && ocean->GetSurfaceHeight(x, z, height) && (!found || height > outHeight)) {
				outHeight = height;
				found = true;
			}
		}
	}
	return found;
}

bool OceanComponent::ApplyMaterialAsset(const std::string& materialPath) {
	IAssetResolver& resolver = GetAssetResolver();
	const std::filesystem::path resolved = resolver.ResolveAssetPath("", materialPath);
	const std::string relative = resolver.MakeProjectRelativePath(resolved);
	materialPath_ = relative.empty() ? materialPath : relative;
	// マテリアルの中身が変わったとき(Inspector で編集)もここが呼ばれるので、必ず読み直す。
	materialDirty_ = true;
	return true;
}

bool OceanComponent::UsesMaterialAsset(const std::string& materialPath) const {
	if (materialPath_.empty()) {
		return false;
	}
	IAssetResolver& resolver = GetAssetResolver();
	const std::filesystem::path target = resolver.ResolveAssetPath("", materialPath);
	const std::filesystem::path current = resolver.ResolveAssetPath("", materialPath_);
	return !target.empty() && !current.empty() && target.lexically_normal() == current.lexically_normal();
}

void OceanComponent::ApplyMaterial() {
	// マテリアルが無ければ、ローポリ・ドット絵向けの既定の見た目(トゥーン3段・面ごとの陰・Water Color)。
	MaterialAssetData material = MaterialAsset::CreateDefault();
	material.shaderModel = static_cast<int>(ShaderModel::kToon);
	material.toonSteps = 3;
	material.flatShading = true;
	material.baseColor = waterColor_;
	if (!materialPath_.empty()) {
		const std::filesystem::path resolved = GetAssetResolver().ResolveAssetPath("", materialPath_);
		MaterialAssetData loaded{};
		std::string message;
		if (!resolved.empty() && MaterialAsset::Load(resolved, loaded, message)) {
			material = loaded;
		}
	}
	if (material.shaderPath.empty()) {
		material.shaderPath = kOceanShaderPath;
	}

	Model& model = *model_;
	model.SetShaderModel(static_cast<ShaderModel>(material.shaderModel));
	model.SetBlendMode(static_cast<BlendMode>(material.blendMode));
	model.SetDepthWrite(material.depthWrite);
	model.SetColor(material.baseColor);
	model.SetTexture(MaterialAsset::ResolveTextureIndex(material, MaterialTextureSlot::BaseColor));
	model.SetEmissiveTexture(MaterialAsset::ResolveTextureIndex(material, MaterialTextureSlot::Emissive));
	model.SetEmissive(material.emissiveColor, material.emissiveIntensity, material.emissiveEnabled);
	model.SetEmissiveBloom(material.bloomIntensity, material.bloomThreshold, material.bloomSoftKnee);
	model.SetUVTransform(material.uvOffset, material.uvScale, material.uvRotation);
	model.SetStylize(material.toonSteps, material.toonSmoothness, material.flatShading, material.pointSampling);
	model.SetCustomShader(MaterialAsset::ResolveCustomShader(material));
	transparent_ = !material.depthWrite;
}

void OceanComponent::EnsureModel() {
	uint32_t divisionsX = 0;
	uint32_t divisionsZ = 0;
	GetGridDivisions(divisionsX, divisionsZ);
	if (!model_ || builtSizeX_ != sizeX_ || builtSizeZ_ != sizeZ_ || builtDivisionsX_ != divisionsX || builtDivisionsZ_ != divisionsZ) {
		model_.reset(Model::CreateGrid("", ShaderModel::kNone, sizeX_, sizeZ_, divisionsX, divisionsZ));
		builtSizeX_ = sizeX_;
		builtSizeZ_ = sizeZ_;
		builtDivisionsX_ = divisionsX;
		builtDivisionsZ_ = divisionsZ;
		materialDirty_ = true;
	}
	// Water Color はマテリアルが無いときの色なので、変えたら反映する。
	if (materialDirty_ || appliedMaterialPath_ != materialPath_ || (materialPath_.empty() && std::memcmp(&appliedWaterColor_, &waterColor_, sizeof(Vector4)) != 0)) {
		ApplyMaterial();
		appliedMaterialPath_ = materialPath_;
		appliedWaterColor_ = waterColor_;
		materialDirty_ = false;
	}
}

void OceanComponent::Draw() {
	GameObject* owner = GetOwner();
	if (!owner || !camera_) {
		return;
	}

	// Play 中でなければ(編集中・一時停止中も含めて Update が回らないとき)、見た目だけ実時間で動かす。フレームに1回だけ。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	if (advancedFrame_ != dxCommon->GetFrameIndex()) {
		advancedFrame_ = dxCommon->GetFrameIndex();
		if (!playing_) {
			waveTime_ += Time::GetUnscaledDeltaTime() * timeScale_;
		}
	}

	EnsureModel();
	// 波は毎フレーム、この Inspector の値をシェーダーへ渡す(両方のビューで同じ値なので、1つのモデルで描ける)。
	model_->SetShaderParams(waves_);
	model_->SetShaderUserValue(foamLevel_);
	model_->SetShaderUserValue2(owner->GetTransform().GetWorldPosition().y);
	model_->SetShaderTime(waveTime_);

	owner->GetTransform().UpdateMatrix(*camera_);
	model_->Draw(owner->GetTransform(), *camera_);
}

} // namespace KujataEngine
