#include "BlobShadowComponent.h"

#include "../3d/Camera.h"
#include "../3d/GraphicsPipeline.h"
#include "../3d/LineRenderer.h"
#include "../assets/MaterialAsset.h"
#include "../base/DirectXCommon.h"
#include "../scene/GameObject.h"
#include "../scene/PhysicsQuery.h"
#include "../scene/Scene.h"
#include "OceanComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KujataEngine {

namespace {

// 地面を探す線の太さ(細い球を落とす)。0 だと Collider の継ぎ目をすり抜けることがあるので少しだけ太らせる。
constexpr float kProbeRadius = 0.02f;
// 影の格子の最大(1辺 16 マス × 三角形2枚 × 3頂点)。
constexpr uint32_t kMaxVertices = 16 * 16 * 6;

} // namespace

bool BlobShadowComponent::GroundHeightAt(float x, float z, float& outHeight) const {
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	if (!scene) {
		return false;
	}
	float best = -std::numeric_limits<float>::infinity();
	// Collider(トリガーは除く。自分と子は無視)へ、真上から細い球を落とす。
	if (receiveColliders_) {
		const Vector3 from = {x, startY_, z};
		const Vector3 to = {x, startY_ - maxDistance_, z};
		SphereCastHit hit;
		if (SphereCast(*scene, from, to, kProbeRadius, hit, owner, false)) {
			best = hit.point.y - kProbeRadius;
		}
	}
	// 海(描かれている海面と同じ高さ)。
	float ocean = 0.0f;
	if (receiveOcean_ && OceanComponent::TryGetSurfaceHeight(*scene, x, z, ocean) && ocean <= startY_) {
		best = (std::max)(best, ocean);
	}
	if (best == -std::numeric_limits<float>::infinity()) {
		return false;
	}
	outHeight = best;
	return true;
}

void BlobShadowComponent::Build() {
	vertices_.clear();
	hasGround_ = false;
	GameObject* owner = GetOwner();
	if (!owner || !owner->GetScene()) {
		return;
	}
	owner->UpdateWorldTransformSelfAndAncestors();
	const Vector3 origin = owner->GetTransform().GetWorldPosition();
	const Vector3 start = {origin.x, origin.y + startHeight_, origin.z};
	startY_ = start.y;
	rayStart_ = start;
	rayEnd_ = {start.x, start.y - maxDistance_, start.z};

	// 1. 真下の地面(Collider と海の高い方)。
	float ground = 0.0f;
	if (!GroundHeightAt(start.x, start.z, ground)) {
		return;
	}
	const float height = (std::max)(start.y - ground, 0.0f);
	const float t = height / (std::max)(maxDistance_, 0.01f);
	if (t >= 1.0f) {
		return;
	}
	hasGround_ = true;
	groundHeight_ = ground;
	rayEnd_ = {start.x, ground, start.z};

	// 2. 地面から離れるほど、小さく・薄くする。
	const float scale = 1.0f + (sizeAtMax_ - 1.0f) * t;
	strength_ = opacity_ * (1.0f + (opacityAtMax_ - 1.0f) * t);
	const float radius = size_ * 0.5f * scale;
	if (radius <= 0.0f || strength_ <= 0.0f) {
		return;
	}

	// 3. 格子の点ごとに真下の地面を調べる。中心の地面より Edge Drop 以上低い点(縁の外)と、地面が無い点は描かない。
	const int cells = std::clamp(resolution_, 1, 16);
	const int points = cells + 1;
	pointHeights_.assign(static_cast<size_t>(points) * points, ground);
	pointValid_.assign(static_cast<size_t>(points) * points, true);
	auto pointX = [&](int ix) { return start.x + (static_cast<float>(ix) / static_cast<float>(cells) * 2.0f - 1.0f) * radius; };
	auto pointZ = [&](int iz) { return start.z + (static_cast<float>(iz) / static_cast<float>(cells) * 2.0f - 1.0f) * radius; };
	for (int iz = 0; iz < points; ++iz) {
		for (int ix = 0; ix < points; ++ix) {
			const size_t index = static_cast<size_t>(iz) * points + ix;
			float y = ground;
			if (!GroundHeightAt(pointX(ix), pointZ(iz), y) || ground - y > edgeDrop_) {
				pointValid_[index] = false;
				continue; // 高さは中心の地面のまま(描かないので形は関係ないが、縁の三角形が垂れ下がらないように)
			}
			pointHeights_[index] = y;
		}
	}

	// 4. 三角形にする。描かない点は法線の x を 0 にして渡し、シェーダーが間を捨てる(縁で影が切れる)。
	auto makeVertex = [&](int ix, int iz) {
		const size_t index = static_cast<size_t>(iz) * points + ix;
		const float u = static_cast<float>(ix) / static_cast<float>(cells);
		const float v = static_cast<float>(iz) / static_cast<float>(cells);
		const float valid = pointValid_[index] ? 1.0f : 0.0f;
		return VertexData{.position = {pointX(ix), pointHeights_[index] + lift_, pointZ(iz), 1.0f}, .texcoord = {u, v}, .normal = {valid, 1.0f, 0.0f}};
	};
	// 上(+Y)から見て表になる並び(Model::CreateGrid と同じ)。
	for (int iz = 0; iz < cells; ++iz) {
		for (int ix = 0; ix < cells; ++ix) {
			vertices_.push_back(makeVertex(ix, iz));
			vertices_.push_back(makeVertex(ix, iz + 1));
			vertices_.push_back(makeVertex(ix + 1, iz));
			vertices_.push_back(makeVertex(ix + 1, iz));
			vertices_.push_back(makeVertex(ix, iz + 1));
			vertices_.push_back(makeVertex(ix + 1, iz + 1));
		}
	}
}

void BlobShadowComponent::Draw() {
	if (!camera_) {
		return;
	}
	// 影の形はカメラに関係ないので、フレームに1回だけ組み立てる(両方のビューで同じ頂点を使う)。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	if (builtFrame_ != dxCommon->GetFrameIndex()) {
		builtFrame_ = dxCommon->GetFrameIndex();
		Build();
		if (!model_) {
			model_.reset(Model::CreateDynamic(kMaxVertices, "", ShaderModel::kNone));
			model_->SetBlendMode(BlendMode::kNormal);
			model_->SetDepthWrite(false);
			model_->SetDoubleSided(true);
			model_->SetCustomShader(GraphicsPipeline::GetInstance()->AcquireCustomShader(MaterialAsset::ResolveShaderPath("engine:Custom/BlobShadow.hlsl")));
		}
		model_->UpdateDynamicVertices(vertices_);
		const Vector4 params[2] = {
		    {color_.x, color_.y, color_.z, strength_},
		    {static_cast<float>(std::clamp(steps_, 1, 8)), 0.0f, 0.0f, 0.0f},
		};
		model_->SetShaderObjectParams(params, std::size(params));
	}

	if (showDebug_) {
		const Vector4 color = hasGround_ ? Vector4{1.0f, 0.9f, 0.1f, 1.0f} : Vector4{1.0f, 0.2f, 0.2f, 1.0f};
		LineRenderer::DrawLine(rayStart_, rayEnd_, color);
	}
	if (!model_ || vertices_.empty()) {
		return;
	}
	if (!identityReady_) {
		identityTransform_.Initialize();
		identityReady_ = true;
	}
	identityTransform_.UpdateMatrix(*camera_);
	model_->Draw(identityTransform_, *camera_);
}

} // namespace KujataEngine
