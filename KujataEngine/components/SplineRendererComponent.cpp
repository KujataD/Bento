#include "SplineRendererComponent.h"

#include "../3d/Camera.h"
#include "../assets/MaterialAsset.h"
#include "../math/MathUtil.h"
#include "../runtime/AssetResolver.h"
#include "../scene/GameObject.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace KujataEngine {

namespace {

Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback) {
	const float length = Length(v);
	return length > 1.0e-6f ? v * (1.0f / length) : fallback;
}

// v に直交する単位ベクトルを1本選ぶ(チューブの最初の断面の向き)。
Vector3 AnyPerpendicular(const Vector3& v) {
	const Vector3 axis = std::fabs(v.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	return SafeNormalize(Cross(axis, v), {1.0f, 0.0f, 0.0f});
}

// Catmull-Rom(centripetal)。p1 から p2 までの間を t(0〜1)で補間する。p0・p3 は前後の点。
// 点の間隔が不揃いでも、行き過ぎて輪になったり尖ったりしにくい(水弾の間隔は速さで変わるため)。
Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	auto knot = [](float previous, const Vector3& a, const Vector3& b) { return previous + (std::max)(std::sqrt(Length(b - a)), 1.0e-4f); };
	const float t0 = 0.0f;
	const float t1 = knot(t0, p0, p1);
	const float t2 = knot(t1, p1, p2);
	const float t3 = knot(t2, p2, p3);
	const float u = t1 + (t2 - t1) * t;

	const Vector3 a1 = p0 * ((t1 - u) / (t1 - t0)) + p1 * ((u - t0) / (t1 - t0));
	const Vector3 a2 = p1 * ((t2 - u) / (t2 - t1)) + p2 * ((u - t1) / (t2 - t1));
	const Vector3 a3 = p2 * ((t3 - u) / (t3 - t2)) + p3 * ((u - t2) / (t3 - t2));
	const Vector3 b1 = a1 * ((t2 - u) / (t2 - t0)) + a2 * ((u - t0) / (t2 - t0));
	const Vector3 b2 = a2 * ((t3 - u) / (t3 - t1)) + a3 * ((u - t1) / (t3 - t1));
	return b1 * ((t2 - u) / (t2 - t1)) + b2 * ((u - t1) / (t2 - t1));
}

VertexData MakeVertex(const Vector3& position, float u, float v, const Vector3& normal) {
	return VertexData{.position = {position.x, position.y, position.z, 1.0f}, .texcoord = {u, v}, .normal = normal};
}

} // namespace

void SplineRendererComponent::OnPlayStart() {
	// 前回の Play でコードから渡された点・値を持ち越さない(コンポーネントは使い回されるため)。
	ClearPoints();
	shaderUserValue_ = 0.0f;
}

void SplineRendererComponent::SetPoints(const std::vector<Vector3>& points, const std::vector<float>* widthScales) {
	codePoints_ = points;
	codeWidthScales_.clear();
	if (widthScales && widthScales->size() == points.size()) {
		codeWidthScales_ = *widthScales;
	}
	codePointsSet_ = true;
}

void SplineRendererComponent::SetShaderObjectParams(const Vector4* values, size_t count) {
	if (!values) {
		shaderObjectParamCount_ = 0;
		return;
	}
	shaderObjectParamCount_ = (std::min)(count, std::size(shaderObjectParams_));
	std::copy(values, values + shaderObjectParamCount_, shaderObjectParams_);
}

void SplineRendererComponent::SetShaderPath(const std::string& shaderPath) { shaderPath_ = shaderPath; }

void SplineRendererComponent::ClearPoints() {
	codePoints_.clear();
	codeWidthScales_.clear();
	codePointsSet_ = false;
}

bool SplineRendererComponent::ApplyMaterialAsset(const std::string& materialPath) {
	IAssetResolver& resolver = GetAssetResolver();
	const std::filesystem::path resolved = resolver.ResolveAssetPath("", materialPath);
	const std::string relative = resolver.MakeProjectRelativePath(resolved);
	materialPath_ = relative.empty() ? materialPath : relative;
	// マテリアルの中身が変わったとき(Inspector で編集)もここが呼ばれるので、必ず読み直す。
	for (bool& dirty : materialDirty_) {
		dirty = true;
	}
	return true;
}

bool SplineRendererComponent::UsesMaterialAsset(const std::string& materialPath) const {
	if (materialPath_.empty()) {
		return false;
	}
	IAssetResolver& resolver = GetAssetResolver();
	const std::filesystem::path target = resolver.ResolveAssetPath("", materialPath);
	const std::filesystem::path current = resolver.ResolveAssetPath("", materialPath_);
	return !target.empty() && !current.empty() && target.lexically_normal() == current.lexically_normal();
}

void SplineRendererComponent::GatherControlPoints() {
	controlPoints_.clear();
	controlWidthScales_.clear();
	if (codePointsSet_) {
		controlPoints_ = codePoints_;
		controlWidthScales_ = codeWidthScales_;
		return;
	}

	// コードから渡されていなければ、子オブジェクトの位置を並び順に使う(エディタで形を作るとき)。
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	for (GameObject* child : owner->GetChildren()) {
		if (child && child->IsActive()) {
			child->UpdateWorldTransformSelfAndAncestors();
			controlPoints_.push_back(child->GetTransform().GetWorldPosition());
		}
	}
}

void SplineRendererComponent::BuildSamples() {
	samples_.clear();
	const size_t count = controlPoints_.size();
	if (count < 2) {
		return;
	}

	const int divisions = (std::max)(subdivisions_, 0) + 1;
	auto pointAt = [&](int index) {
		// 端の外側は、端の点を反対側へ伸ばした仮の点にする(端でも曲がり方が自然になる)。
		if (index < 0) {
			return controlPoints_[0] * 2.0f - controlPoints_[1];
		}
		if (index >= static_cast<int>(count)) {
			return controlPoints_[count - 1] * 2.0f - controlPoints_[count - 2];
		}
		return controlPoints_[index];
	};
	auto widthScaleAt = [&](size_t index) { return controlWidthScales_.empty() ? 1.0f : controlWidthScales_[index]; };

	for (size_t segment = 0; segment + 1 < count; ++segment) {
		const int i = static_cast<int>(segment);
		for (int step = 0; step < divisions; ++step) {
			const float t = static_cast<float>(step) / static_cast<float>(divisions);
			Sample sample;
			sample.position = subdivisions_ > 0 ? CatmullRom(pointAt(i - 1), pointAt(i), pointAt(i + 1), pointAt(i + 2), t) : Lerp(pointAt(i), pointAt(i + 1), t);
			sample.width = Lerp(widthScaleAt(segment), widthScaleAt(segment + 1), t);
			samples_.push_back(sample);
		}
	}
	Sample last;
	last.position = controlPoints_[count - 1];
	last.width = widthScaleAt(count - 1);
	samples_.push_back(last);

	// 長さに沿った u と、全体の中での位置(太さの補間用)を求める。
	std::vector<float> distances(samples_.size(), 0.0f);
	for (size_t i = 1; i < samples_.size(); ++i) {
		distances[i] = distances[i - 1] + Length(samples_[i].position - samples_[i - 1].position);
	}
	const float totalLength = (std::max)(distances.back(), 1.0e-6f);
	curveLength_ = distances.back();
	for (size_t i = 0; i < samples_.size(); ++i) {
		Sample& sample = samples_[i];
		const float ratio = distances[i] / totalLength;
		sample.u = uvPerUnit_ > 0.0f ? distances[i] * uvPerUnit_ : ratio;
		sample.width *= Lerp(startWidth_, endWidth_, ratio);

		// 進む向きは前後の点から取る(端は片側だけ)。
		const Vector3& previous = samples_[i > 0 ? i - 1 : i].position;
		const Vector3& next = samples_[i + 1 < samples_.size() ? i + 1 : i].position;
		sample.tangent = SafeNormalize(next - previous, {0.0f, 0.0f, 1.0f});
	}
}

void SplineRendererComponent::BuildTube() {
	vertices_.clear();
	if (samples_.size() < 2) {
		return;
	}
	const int sides = std::clamp(sides_, 3, 32);

	// 断面の向き(normal/binormal)を曲線に沿って運ぶ。前の断面の向きを今の進行方向に直交するよう
	// 直すだけなので、急に裏返ったりねじれたりしない(ローポリだとねじれが目立つため)。
	std::vector<Vector3> rings;
	rings.reserve(samples_.size() * sides);
	std::vector<Vector3> ringNormals;
	ringNormals.reserve(samples_.size() * sides);
	Vector3 frameNormal = AnyPerpendicular(samples_[0].tangent);
	for (const Sample& sample : samples_) {
		frameNormal = SafeNormalize(frameNormal - sample.tangent * Dot(frameNormal, sample.tangent), AnyPerpendicular(sample.tangent));
		const Vector3 binormal = Cross(sample.tangent, frameNormal);
		const float radius = sample.width * 0.5f;
		for (int k = 0; k < sides; ++k) {
			const float angle = static_cast<float>(k) / static_cast<float>(sides) * 2.0f * std::numbers::pi_v<float>;
			const Vector3 direction = frameNormal * std::cos(angle) + binormal * std::sin(angle);
			rings.push_back(sample.position + direction * radius);
			ringNormals.push_back(direction);
		}
	}

	// 隣り合う断面の間を四角形(三角形2枚)でつなぐ。周の最後は最初へ戻る。
	for (size_t i = 0; i + 1 < samples_.size(); ++i) {
		for (int k = 0; k < sides; ++k) {
			const int k1 = (k + 1) % sides;
			const float v0 = static_cast<float>(k) / static_cast<float>(sides);
			const float v1 = static_cast<float>(k + 1) / static_cast<float>(sides);
			const size_t a = i * sides + k;
			const size_t b = i * sides + k1;
			const size_t c = (i + 1) * sides + k;
			const size_t d = (i + 1) * sides + k1;
			const float u0 = samples_[i].u;
			const float u1 = samples_[i + 1].u;
			// 外から見て時計回り(表)になる並び(a→b→c の外積が外を向く)。
			vertices_.push_back(MakeVertex(rings[a], u0, v0, ringNormals[a]));
			vertices_.push_back(MakeVertex(rings[b], u0, v1, ringNormals[b]));
			vertices_.push_back(MakeVertex(rings[c], u1, v0, ringNormals[c]));
			vertices_.push_back(MakeVertex(rings[b], u0, v1, ringNormals[b]));
			vertices_.push_back(MakeVertex(rings[d], u1, v1, ringNormals[d]));
			vertices_.push_back(MakeVertex(rings[c], u1, v0, ringNormals[c]));
		}
	}

	// 両端のふた(中心から周への扇)。法線は進む向き(先端)と逆向き(根元)。
	if (caps_) {
		auto addCap = [&](size_t sampleIndex, bool front) {
			const Sample& sample = samples_[sampleIndex];
			const Vector3 normal = front ? sample.tangent : -sample.tangent;
			for (int k = 0; k < sides; ++k) {
				const Vector3& p0 = rings[sampleIndex * sides + k];
				const Vector3& p1 = rings[sampleIndex * sides + (k + 1) % sides];
				vertices_.push_back(MakeVertex(sample.position, sample.u, 0.5f, normal));
				vertices_.push_back(MakeVertex(front ? p0 : p1, sample.u, 0.0f, normal));
				vertices_.push_back(MakeVertex(front ? p1 : p0, sample.u, 1.0f, normal));
			}
		};
		addCap(0, false);
		addCap(samples_.size() - 1, true);
	}
}

void SplineRendererComponent::BuildRibbon(const Camera& camera) {
	vertices_.clear();
	if (samples_.size() < 2) {
		return;
	}
	const Vector3 eye = camera.translation_;
	std::vector<Vector3> left(samples_.size());
	std::vector<Vector3> right(samples_.size());
	std::vector<Vector3> normals(samples_.size());
	for (size_t i = 0; i < samples_.size(); ++i) {
		const Sample& sample = samples_[i];
		// 横の向き = 進む向き × 視線。これで帯が常にカメラの方を向く。
		const Vector3 toEye = SafeNormalize(eye - sample.position, {0.0f, 1.0f, 0.0f});
		const Vector3 side = SafeNormalize(Cross(sample.tangent, toEye), AnyPerpendicular(sample.tangent));
		const float halfWidth = sample.width * 0.5f;
		left[i] = sample.position + side * halfWidth;
		right[i] = sample.position - side * halfWidth;
		normals[i] = toEye;
	}
	for (size_t i = 0; i + 1 < samples_.size(); ++i) {
		const float u0 = samples_[i].u;
		const float u1 = samples_[i + 1].u;
		vertices_.push_back(MakeVertex(left[i], u0, 0.0f, normals[i]));
		vertices_.push_back(MakeVertex(left[i + 1], u1, 0.0f, normals[i + 1]));
		vertices_.push_back(MakeVertex(right[i], u0, 1.0f, normals[i]));
		vertices_.push_back(MakeVertex(right[i], u0, 1.0f, normals[i]));
		vertices_.push_back(MakeVertex(left[i + 1], u1, 0.0f, normals[i + 1]));
		vertices_.push_back(MakeVertex(right[i + 1], u1, 1.0f, normals[i + 1]));
	}
}

void SplineRendererComponent::ApplyMaterial(Model& model) {
	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!materialPath_.empty()) {
		const std::filesystem::path resolved = GetAssetResolver().ResolveAssetPath("", materialPath_);
		MaterialAssetData loaded{};
		std::string message;
		if (!resolved.empty() && MaterialAsset::Load(resolved, loaded, message)) {
			material = loaded;
		}
	}

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
	// Material が無いときは、Shader だけを差し替える(マテリアルのファイルを作らずに使えるように)。
	if (materialPath_.empty() && !shaderPath_.empty()) {
		material.shaderPath = shaderPath_;
	}
	model.SetCustomShader(MaterialAsset::ResolveCustomShader(material));
	model.SetShaderParams(material.shaderParams);
	// リボンは裏から見えることがある(急に曲がるところ)ので両面にする。チューブは閉じているので表だけ。
	model.SetDoubleSided(static_cast<Shape>(shape_) == Shape::Ribbon);
	transparent_ = !material.depthWrite;
}

Model* SplineRendererComponent::EnsureModel(uint32_t viewIndex, size_t vertexCount) {
	std::unique_ptr<Model>& model = models_[viewIndex];
	// 確保済みの頂点数が足りなければ、余裕を持って作り直す(毎フレーム作り直さないように倍にする)。
	if (!model || model->GetDynamicVertexCapacity() < vertexCount) {
		const size_t current = model ? model->GetDynamicVertexCapacity() : 0;
		const uint32_t capacity = static_cast<uint32_t>((std::max)({vertexCount, current * 2, static_cast<size_t>(256)}));
		model.reset(Model::CreateDynamic(capacity, "", ShaderModel::kNone));
		materialDirty_[viewIndex] = true;
	}
	if (materialDirty_[viewIndex] || appliedMaterialPath_[viewIndex] != materialPath_ || appliedShaderPath_ != shaderPath_) {
		ApplyMaterial(*model);
		appliedMaterialPath_[viewIndex] = materialPath_;
		appliedShaderPath_ = shaderPath_;
		materialDirty_[viewIndex] = false;
	}
	return model.get();
}

void SplineRendererComponent::Draw() {
	if (!camera_) {
		return;
	}
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	const uint32_t viewIndex = dxCommon->GetRenderViewIndex();
	if (viewIndex >= DirectXCommon::kRenderViewCount) {
		return;
	}

	// 形はビューごとに、フレームで1回だけ組み立てる(同じフレームで同じ頂点バッファを2回書くと、
	// 先に積んだ描画も後の中身で描かれてしまうため、ビューごとに別のモデルを持つ)。
	// Draw で組み立てるのは、編集中(Play していない = Update が回らない)でも形を確かめられるようにするため。
	if (builtFrame_[viewIndex] != dxCommon->GetFrameIndex()) {
		builtFrame_[viewIndex] = dxCommon->GetFrameIndex();
		GatherControlPoints();
		BuildSamples();
		if (static_cast<Shape>(shape_) == Shape::Ribbon) {
			BuildRibbon(*camera_);
		} else {
			BuildTube();
		}
		Model* model = EnsureModel(viewIndex, vertices_.size());
		model->UpdateDynamicVertices(vertices_);
		model->SetShaderCurveLength(controlPoints_.size() >= 2 ? curveLength_ : 0.0f);
		model->SetShaderUserValue(shaderUserValue_);
		if (shaderObjectParamCount_ > 0) {
			model->SetShaderObjectParams(shaderObjectParams_, shaderObjectParamCount_);
		}
	}

	Model* model = models_[viewIndex].get();
	if (!model || vertices_.empty()) {
		return;
	}
	if (!identityReady_) {
		identityTransform_.Initialize();
		identityReady_ = true;
	}
	identityTransform_.translation_ = {0.0f, 0.0f, 0.0f};
	identityTransform_.rotation_ = {0.0f, 0.0f, 0.0f};
	identityTransform_.scale_ = {1.0f, 1.0f, 1.0f};
	identityTransform_.UpdateMatrix(*camera_);
	model->Draw(identityTransform_, *camera_);
}

} // namespace KujataEngine
