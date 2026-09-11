#include "ParticleSystemComponent.h"

#include "../3d/Camera.h"
#include "../base/Time.h"
#include "../math/MathUtil.h"
#include "../base/ProjectPath.h"
#include "../scene/GameObject.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <filesystem>

namespace KujataEngine {

namespace {

constexpr float kDegreeToRadian = std::numbers::pi_v<float> / 180.0f;

Vector4 LerpColor(const Vector4& from, const Vector4& to, float t) {
	return {
	    from.x + (to.x - from.x) * t,
	    from.y + (to.y - from.y) * t,
	    from.z + (to.z - from.z) * t,
	    from.w + (to.w - from.w) * t,
	};
}

Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (length <= 1.0e-5f) {
		return fallback;
	}
	return {v.x / length, v.y / length, v.z / length};
}

/// <summary>vに垂直な単位ベクトルを1本作る。コーン状に散らすときの基準軸に使う。</summary>
Vector3 AnyPerpendicular(const Vector3& v) {
	// vと平行になりにくい軸を選んでから外積を取る。同じ軸を固定で使うと、
	// vがその軸と平行になった瞬間に長さ0になって散らばりが壊れる。
	Vector3 axis = (std::fabs(v.y) < 0.9f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	Vector3 perpendicular = {
	    axis.y * v.z - axis.z * v.y,
	    axis.z * v.x - axis.x * v.z,
	    axis.x * v.y - axis.y * v.x,
	};
	return NormalizeOr(perpendicular, {1.0f, 0.0f, 0.0f});
}

} // namespace

void ParticleSystemComponent::Initialize() {
	// ランタイムにPrefabから生成された場合はOnPlayStartが来ないので、ここでも用意しておく。
	EnsureModel();
}

void ParticleSystemComponent::OnPlayStart() {
	particles_.clear();
	emitAccumulator_ = 0.0f;
	emitting_ = true;
	strength_ = 1.0f;
	hasColorOverride_ = false;
	random_.seed(12345u);
}

void ParticleSystemComponent::OnPlayStop() {
	particles_.clear();
	emitAccumulator_ = 0.0f;
}

void ParticleSystemComponent::SetColorOverride(const Vector4& color) {
	hasColorOverride_ = true;
	colorOverride_ = color;
}

void ParticleSystemComponent::EnsureModel() {
	if (model_) {
		return;
	}
	// 形ごとに専用のParticleModelを持つ。**1コンポーネント1モデル**で、
	// そのぶんインスタンシング用のSRVを1枚消費する点に注意(大量に置くなら共有化が要る)。
	// **TextureManagerは絶対パスしか受け付けない。**
	// 相対のまま渡すとWICの読み込みが失敗し、assert(false)でPlay開始時に止まる。
	// Inspectorには "Resources/white1x1.png" のような書きやすい相対パスを入れたいので、ここで解決する。
	std::filesystem::path texturePath(texturePath_);
	if (texturePath.is_relative()) {
		texturePath = GetProjectDataRoot() / texturePath;
	}
	std::string resolved = texturePath.generic_string();

	ParticleModel* created = nullptr;
	switch (shape_) {
	case 1:
		created = ParticleModel::CreateCube(resolved);
		break;
	case 2:
		created = ParticleModel::CreateTriangle(resolved);
		break;
	case 3:
		created = ParticleModel::CreateTetrahedron(resolved);
		break;
	default:
		created = ParticleModel::CreatePlane(resolved);
		break;
	}
	model_.reset(created);
	if (model_) {
		model_->SetBlendMode((blendMode_ == 1) ? BlendMode::kAdd : BlendMode::kNormal);
	}
}

float ParticleSystemComponent::RandomRange(float minValue, float maxValue) const {
	if (maxValue <= minValue) {
		return minValue;
	}
	std::uniform_real_distribution<float> distribution(minValue, maxValue);
	return distribution(random_);
}

Vector3 ParticleSystemComponent::SampleEmitPosition(const Vector3& origin) const {
	if (emitRadius_ <= 0.0f || emitVolume_ == 0) {
		return origin;
	}

	float angle = RandomRange(0.0f, std::numbers::pi_v<float> * 2.0f);
	// 半径を一様乱数で取ると中心に密集するので、面積(体積)で均すために平方根を掛ける。
	float radius = emitRadius_ * std::sqrt(RandomRange(0.0f, 1.0f));

	if (emitVolume_ == 2) {
		// 水平の円盤。地面に沿って広がる土埃向き。
		return origin + Vector3{std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
	}

	// 球。上下にも散る。
	float height = RandomRange(-1.0f, 1.0f);
	float planar = std::sqrt((std::max)(0.0f, 1.0f - height * height));
	return origin + Vector3{std::cos(angle) * planar * radius, height * radius, std::sin(angle) * planar * radius};
}

Vector3 ParticleSystemComponent::SampleVelocity(const Vector3& baseDirection) const {
	Vector3 forward = NormalizeOr(baseDirection, {0.0f, 1.0f, 0.0f});
	float speed = RandomRange(speedMin_, speedMax_) * strength_;

	if (spreadAngleDeg_ <= 0.0f) {
		return forward * speed;
	}

	// 基準方向を軸としたコーンの内側へランダムに振る。
	float maxAngle = std::clamp(spreadAngleDeg_, 0.0f, 180.0f) * kDegreeToRadian;
	// cosを一様に取ると立体角が均等になる(角度を一様に取ると中心が濃くなる)。
	float cosAngle = RandomRange(std::cos(maxAngle), 1.0f);
	float sinAngle = std::sqrt((std::max)(0.0f, 1.0f - cosAngle * cosAngle));
	float roll = RandomRange(0.0f, std::numbers::pi_v<float> * 2.0f);

	Vector3 right = AnyPerpendicular(forward);
	Vector3 up = {
	    forward.y * right.z - forward.z * right.y,
	    forward.z * right.x - forward.x * right.z,
	    forward.x * right.y - forward.y * right.x,
	};

	Vector3 direction = forward * cosAngle + (right * std::cos(roll) + up * std::sin(roll)) * sinAngle;
	return NormalizeOr(direction, forward) * speed;
}

void ParticleSystemComponent::SpawnOne() {
	if (!owner_) {
		return;
	}

	owner_->UpdateWorldTransformSelfAndAncestors();
	WorldTransform& transform = owner_->GetTransform();
	Vector3 origin = transform.GetWorldPosition();
	// 基準方向はオーナーのローカル指定。向きを変えれば噴射方向も一緒に回る。
	Vector3 baseDirection = transform.GetRotationQuaternion().RotateVector(emitDirection_);

	Particle particle{};
	particle.position = SampleEmitPosition(origin);
	particle.velocity = SampleVelocity(baseDirection);
	particle.lifetime = (std::max)(RandomRange(lifetimeMin_, lifetimeMax_), 0.02f);
	particle.age = 0.0f;
	// 強さは大きさにも効かせる。数だけ増やすと「粒が多いだけ」で迫力にならない。
	particle.startSize = RandomRange(sizeMin_, sizeMax_) * (0.6f + 0.4f * strength_);
	particle.rotation = RandomRange(0.0f, std::numbers::pi_v<float> * 2.0f);
	particle.rotationSpeed = RandomRange(-rotationSpeedDeg_, rotationSpeedDeg_) * kDegreeToRadian;
	particles_.push_back(particle);
}

void ParticleSystemComponent::Burst() { Burst(burstCount_); }

void ParticleSystemComponent::Burst(int count) {
	// 強さは個数にも掛かる。0.5倍なら半分の粒しか出ない。
	int scaled = static_cast<int>(std::lround(static_cast<float>(count) * strength_));
	scaled = std::clamp(scaled, 0, 2000);
	for (int index = 0; index < scaled; ++index) {
		SpawnOne();
	}
}

void ParticleSystemComponent::Update() {
	// **モデル(=テクスチャ読み込み)の生成は必ず描画パスの外で行う。**
	// Draw()の中で読むと、コマンドリストを積んでいる最中にディスクリプタを触ることになる。
	EnsureModel();

	float deltaTime = Time::GetDeltaTime();
	if (deltaTime <= 0.0f) {
		return;
	}

	// --- 持続発生 ---
	if (looping_ && emitting_ && emissionRate_ > 0.0f) {
		emitAccumulator_ += emissionRate_ * strength_ * deltaTime;
		// 端数を持ち越す。切り捨てだけにするとフレームレートで総量が変わってしまう。
		while (emitAccumulator_ >= 1.0f) {
			emitAccumulator_ -= 1.0f;
			SpawnOne();
		}
	}

	// --- 粒の更新 ---
	// 追従モードのときは、オーナーの移動ぶんだけ粒も一緒に運ぶ。
	Vector3 followDelta = {0.0f, 0.0f, 0.0f};
	if (!worldSpace_ && owner_) {
		Vector3 current = owner_->GetTransform().GetWorldPosition();
		followDelta = current - lastOwnerPosition_;
		lastOwnerPosition_ = current;
	} else if (owner_) {
		lastOwnerPosition_ = owner_->GetTransform().GetWorldPosition();
	}

	float dragFactor = std::exp(-(std::max)(drag_, 0.0f) * deltaTime);
	for (Particle& particle : particles_) {
		particle.age += deltaTime;
		particle.velocity = particle.velocity * dragFactor + gravity_ * deltaTime;
		particle.position += particle.velocity * deltaTime + followDelta;
		particle.rotation += particle.rotationSpeed * deltaTime;
	}

	particles_.erase(
	    std::remove_if(particles_.begin(), particles_.end(),
	        [](const Particle& particle) { return particle.age >= particle.lifetime; }),
	    particles_.end());
}

void ParticleSystemComponent::Draw() {
	if (particles_.empty() || !camera_) {
		return;
	}
	if (!model_) {
		return;
	}

	Vector4 from = hasColorOverride_ ? Vector4{colorOverride_.x, colorOverride_.y, colorOverride_.z, startColor_.w} : startColor_;
	Vector4 to = hasColorOverride_ ? Vector4{colorOverride_.x, colorOverride_.y, colorOverride_.z, endColor_.w} : endColor_;

	for (const Particle& particle : particles_) {
		float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		float size = particle.startSize * (1.0f + (endSizeScale_ - 1.0f) * t);
		Vector3 scale = {size, size, size};
		// Planeはビルボードして常にカメラを向ける。zの自転がロール方向の回転になる。
		Vector3 rotation = {0.0f, 0.0f, particle.rotation};

		TransformationMatrix matrix = MakeBillboardMatrix(scale, rotation, particle.position, *camera_);
		if (!model_->AddInstanceParticle(matrix, LerpColor(from, to, t))) {
			// 上限に達した。これ以上積んでも描かれないので打ち切る。
			break;
		}
	}

	model_->UpdateBuffer();
	ParticleModel::PreDraw();
	model_->Draw();
	ParticleModel::PostDraw();
}

} // namespace KujataEngine
