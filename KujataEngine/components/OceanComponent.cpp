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
	if (!followCamera_ && (local.x < -halfX || local.x > halfX || local.z < -halfZ || local.z > halfZ)) {
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
	// Follow Camera のときは板がマス単位でずれるだけなので、マスの番号は板の外まで続けて数えてよい。
	const float ix = followCamera_ ? std::floor(gx) : std::clamp(std::floor(gx), 0.0f, static_cast<float>(divisionsX - 1));
	const float iz = followCamera_ ? std::floor(gz) : std::clamp(std::floor(gz), 0.0f, static_cast<float>(divisionsZ - 1));
	const float fx = gx - ix;
	const float fz = gz - iz;

	// 格子の頂点(ローカル)の、ワールドでの高さ = 板の高さ + 波。
	auto vertexHeight = [&](float cx, float cz) {
		const Vector3 p = Transform(Vector3{-halfX + cx * cellX, 0.0f, -halfZ + cz * cellZ}, world);
		return p.y + EvaluateWaves({p.x, p.z});
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
	// マテリアルが無ければ、ローポリ・ドット絵向けの既定の見た目(トゥーン3段・面ごとの陰・模様をぼかさない)。
	// 海の色は Inspector の色で決まり、マテリアルの色はそれに掛け算する(既定は白 = そのまま)。
	MaterialAssetData material = MaterialAsset::CreateDefault();
	material.shaderModel = static_cast<int>(ShaderModel::kToon);
	material.toonSteps = 3;
	material.flatShading = true;
	material.pointSampling = true;
	usePatternTexture_ = false;
	if (!materialPath_.empty()) {
		const std::filesystem::path resolved = GetAssetResolver().ResolveAssetPath("", materialPath_);
		MaterialAssetData loaded{};
		std::string message;
		if (!resolved.empty() && MaterialAsset::Load(resolved, loaded, message)) {
			material = loaded;
			// 既定の白以外のテクスチャが入っていれば、それを泡の模様にする。
			const std::string texturePath = MaterialAsset::GetTexturePath(material, MaterialTextureSlot::BaseColor);
			usePatternTexture_ = !texturePath.empty() && texturePath.find("white1x1") == std::string::npos;
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
	if (materialDirty_ || appliedMaterialPath_ != materialPath_) {
		ApplyMaterial();
		appliedMaterialPath_ = materialPath_;
		materialDirty_ = false;
	}
}

void OceanComponent::BuildShaderParams(Vector4 (&outParams)[11]) const {
	// 並びは Ocean.hlsl の先頭のコメントと一致させる。
	for (int i = 0; i < kWaveCount; ++i) {
		outParams[i] = waves_[i];
	}
	auto rgbw = [](const Vector4& color, float w) { return Vector4{color.x, color.y, color.z, w}; };
	outParams[4] = rgbw(waterColor_, lighting_);
	outParams[5] = rgbw(shallowColor_, depthColorDistance_);
	outParams[6] = rgbw(foamColor_, shoreFoamDistance_);
	outParams[7] = rgbw(ringColor_, foamLevel_);
	outParams[8] = {patternSize_, foamAmount_, foamWidth_, ringOffset_};
	outParams[9] = {distortStrength_, distortLength_, driftSpeed_, usePatternTexture_ ? 1.0f : 0.0f};
	const GameObject* owner = GetOwner();
	const float baseHeight = owner ? owner->GetTransform().GetWorldPosition().y : 0.0f;
	outParams[10] = {baseHeight, shallowAlpha_, driftDirection_, 0.0f};
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
	// 波と見た目は毎フレーム、この Inspector の値をシェーダーへ渡す(両方のビューで同じ値なので、1つのモデルで描ける)。
	Vector4 params[11];
	BuildShaderParams(params);
	model_->SetShaderObjectParams(params, std::size(params));
	model_->SetShaderTime(waveTime_);

	// 板は、このオブジェクトの子として置いた描画用の Transform で描く。
	// Follow Camera なら、カメラの真下へ「マスの大きさ単位で」ずらす(細かくずらすと頂点が波の上を滑って、波が泳いで見える)。
	// 波と模様はワールド座標で決めているので、板が動いても海はその場に留まって見える。
	if (!drawTransformReady_) {
		drawTransform_.Initialize();
		drawTransformReady_ = true;
	}
	WorldTransform& ownerTransform = owner->GetTransform();
	ownerTransform.UpdateWorldMatrix();
	drawTransform_.parent_ = &ownerTransform;
	drawTransform_.translation_ = {0.0f, 0.0f, 0.0f};
	if (followCamera_) {
		const Vector3 cameraLocal = Transform(camera_->translation_, Inverse(ownerTransform.matWorld_));
		const float cellX = sizeX_ / static_cast<float>(builtDivisionsX_);
		const float cellZ = sizeZ_ / static_cast<float>(builtDivisionsZ_);
		drawTransform_.translation_ = {std::round(cameraLocal.x / cellX) * cellX, 0.0f, std::round(cameraLocal.z / cellZ) * cellZ};
	}
	drawTransform_.UpdateMatrix(*camera_);
	model_->Draw(drawTransform_, *camera_);
}

} // namespace KujataEngine
