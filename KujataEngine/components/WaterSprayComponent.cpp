#include "WaterSprayComponent.h"

#include "../3d/Camera.h"
#include "../base/Time.h"
#include "../base/WinApp.h"
#include "../math/MathUtil.h"
#include "../runtime/InputActionSystem.h"
#include "../runtime/UIInput.h"
#include "../scene/GameObject.h"
#include "../scene/PhysicsQuery.h"
#include "../scene/Scene.h"
#include "ColliderComponent.h"
#include "ModelRendererComponent.h"
#include "OceanComponent.h"
#include "WaterDropComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace KujataEngine {

namespace {

// 水流の見た目に使う、エンジン同梱のシェーダー。
constexpr const char* kWaterShaderPath = "engine:Custom/Water.hlsl";
// 当たった点を先端として使い続ける時間[秒]。
constexpr float kHitHoldTime = 0.1f;

} // namespace

void WaterSprayComponent::OnPlayStart() {
	// 実行中に作ったものは Play を止めたときに片付けられるので、覚え直すだけ。
	streams_.clear();
	freeDrops_.clear();
	flashes_.clear();
	dropRoot_ = nullptr;
	streamRoot_ = nullptr;
	dropSerial_ = 0;
	activeStream_ = -1;
	nextStream_ = 0;
	emitTimer_ = 0.0f;
	dropletAccumulator_ = 0.0f;
	firing_ = false;
	codeFiring_ = false;
	hasLastHit_ = false;
	random_.seed(20260930u);
	particles_ = GetOwner() ? GetOwner()->GetComponent<ParticleSystemComponent>() : nullptr;
}

void WaterSprayComponent::SetAimDirection(const Vector3& direction) {
	if (Length(direction) > 1.0e-6f) {
		codeAimDirection_ = Normalize(direction);
	}
}

void WaterSprayComponent::SetAimPoint(const Vector3& point) {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	SetAimDirection(point - owner->GetTransform().GetWorldPosition());
}

bool WaterSprayComponent::TryGetLastHitPoint(Vector3& outPoint) const {
	if (!hasLastHit_) {
		return false;
	}
	outPoint = lastHitPoint_;
	return true;
}

void WaterSprayComponent::ApplyLook(SplineRendererComponent& spline) const {
	// マテリアルを作らなくても水に見えるよう、見た目の値はここから直接シェーダーへ渡す(Water.hlsl の先頭のコメントの並び)。
	const Vector4 params[6] = {
	    {wobbleAmount_, wobblePerMeter_, wobbleSpeed_, static_cast<float>(sides_)},
	    {stripesPerMeter_, stripeSpeed_, stripeLength_, rimWidth_},
	    {foamColor_.x, foamColor_.y, foamColor_.z, dissolveStart_},
	    {rimColor_.x, rimColor_.y, rimColor_.z, dissolveCells_},
	    waterColor_,
	    {static_cast<float>(toonSteps_), flatShading_ ? 1.0f : 0.0f, 0.0f, 0.0f},
	};
	spline.SetShaderObjectParams(params, std::size(params));
}

void WaterSprayComponent::EnsureStreams() {
	if (!streams_.empty()) {
		return;
	}
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	if (!scene) {
		return;
	}

	// 子に SplineRenderer があればそれを使う(見た目を自分で作り込みたいとき)。
	for (GameObject* child : owner->GetChildren()) {
		if (SplineRendererComponent* spline = child ? child->GetComponent<SplineRendererComponent>() : nullptr) {
			spline->SetPoints({}); // 点0個 = 何も描かない(子の位置を点として使わせない)
			streams_.push_back(Stream{spline});
		}
	}
	if (!streams_.empty()) {
		return;
	}

	// 無ければ自分で作る(付けるだけで使えるように)。実行中だけの物なので、Play を止めると消える。
	streamRoot_ = scene->CreateGameObject("WaterStreams (" + owner->GetName() + ")");
	const int count = std::clamp(streamCount_, 1, 16);
	for (int index = 0; index < count; ++index) {
		GameObject* object = scene->CreateGameObject("Stream" + std::to_string(index));
		object->SetParent(streamRoot_);
		SplineRendererComponent* spline = object->AddComponent<SplineRendererComponent>();
		spline->SetShaderPath(kWaterShaderPath);
		spline->SetPoints({});
		streams_.push_back(Stream{spline});
	}
}

void WaterSprayComponent::EnsureDropFolders() {
	if (dropRoot_) {
		return;
	}
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	if (!scene) {
		return;
	}
	// ノズルの子にしないのは、ノズルが拡大縮小・回転されていると、水弾の位置と判定の大きさがずれるため。
	// ルートに置けば、水弾の Transform の値がそのままワールド座標になる。
	dropRoot_ = scene->CreateGameObject("WaterDrops (" + owner->GetName() + ")");
	for (size_t index = 0; index < streams_.size(); ++index) {
		GameObject* folder = scene->CreateGameObject("Stream" + std::to_string(index));
		folder->SetParent(dropRoot_);
		streams_[index].dropFolder = folder;
	}
}

void WaterSprayComponent::SpawnDrop(Stream& stream, const Vector3& position, const Vector3& velocity, float age) {
	Drop drop;
	if (!freeDrops_.empty()) {
		drop = freeDrops_.back();
		freeDrops_.pop_back();
	} else {
		Scene* scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
		if (!scene) {
			return;
		}
		char name[32];
		std::snprintf(name, sizeof(name), "Drop %03d", dropSerial_++);
		drop.object = scene->CreateGameObject(name);
		// 当たり判定の形(確認用。選ぶとギズモで見える)。トリガーにして、衝突マスク0で他の物と押し合わないようにする。
		SphereColliderComponent* collider = drop.object->AddComponent<SphereColliderComponent>();
		collider->SetTrigger(true);
		collider->SetCollisionMask(0);
		drop.data = drop.object->AddComponent<WaterDropComponent>();
	}
	drop.object->SetParent(stream.dropFolder);
	drop.object->SetActive(true);
	drop.object->GetTransform().translation_ = position;
	if (SphereColliderComponent* collider = drop.object->GetComponent<SphereColliderComponent>()) {
		collider->SetRadius(hitRadius_);
	}
	drop.data->SetVelocity(velocity);
	drop.data->SetAge(age);
	drop.data->SetHit(false);
	stream.drops.push_back(drop);
}

void WaterSprayComponent::ReleaseDrop(const Drop& drop) {
	// 実行中にシーンから消すと、更新ループの途中で配列が詰まって危ないので、非アクティブにして使い回す。
	drop.object->SetActive(false);
	freeDrops_.push_back(drop);
}

bool WaterSprayComponent::ShouldFire() const {
	switch (static_cast<FireMode>(fireMode_)) {
	case FireMode::Always:
		return true;
	case FireMode::Code:
		return codeFiring_;
	case FireMode::Action:
	default:
		// 生の入力は読まない。アクション(InputActionSystem)を見る。
		return !fireAction_.empty() && InputActionSystem::GetInstance()->GetActions().Held(fireAction_);
	}
}

bool WaterSprayComponent::GetMouseAimPoint(const Vector3& nozzle, Vector3& outPoint) const {
	const UIPointerState& pointer = GetUIPointer();
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	Camera* camera = scene ? scene->GetGameViewCamera() : nullptr;
	if (!camera || !pointer.inside) {
		return false;
	}

	// マウスの位置(Game ビューの出力の座標)を画面の -1〜1 に直し、カメラの逆行列でワールドの線にする。
	const float ndcX = pointer.x / static_cast<float>(WinApp::kWindowWidth) * 2.0f - 1.0f;
	const float ndcY = 1.0f - pointer.y / static_cast<float>(WinApp::kWindowHeight) * 2.0f;
	const Matrix4x4 inverseViewProjection = Inverse(camera->matView * camera->matProjection);
	const Vector3 nearPoint = Transform({ndcX, ndcY, 0.0f}, inverseViewProjection);
	const Vector3 farPoint = Transform({ndcX, ndcY, 1.0f}, inverseViewProjection);
	const Vector3 direction = farPoint - nearPoint;

	// ノズルを通り、カメラの向きに垂直な平面と交わる点。
	const Vector3 cameraForward = Normalize(Vector3{camera->matView.m[0][2], camera->matView.m[1][2], camera->matView.m[2][2]});
	const float denominator = Dot(direction, cameraForward);
	if (std::fabs(denominator) < 1.0e-6f) {
		return false;
	}
	const float t = Dot(nozzle - nearPoint, cameraForward) / denominator;
	outPoint = nearPoint + direction * t;
	return true;
}

bool WaterSprayComponent::GetAimDirection(const Vector3& nozzle, Vector3& outDirection) {
	GameObject* owner = GetOwner();
	if (!owner) {
		return false;
	}
	Vector3 direction{};
	switch (static_cast<AimMode>(aimMode_)) {
	case AimMode::Target: {
		GameObject* target = aimTarget_.Get();
		if (!target) {
			return false;
		}
		target->UpdateWorldTransformSelfAndAncestors();
		direction = target->GetTransform().GetWorldPosition() - nozzle;
		break;
	}
	case AimMode::Mouse: {
		Vector3 point{};
		if (!GetMouseAimPoint(nozzle, point)) {
			return false;
		}
		direction = point - nozzle;
		break;
	}
	case AimMode::Code:
		direction = codeAimDirection_;
		break;
	case AimMode::Forward:
	default:
		// このオブジェクトの前(Z+ を回転させた向き)。
		direction = TransformNormal({0.0f, 0.0f, 1.0f}, owner->GetTransform().matWorld_);
		break;
	}
	if (Length(direction) <= 1.0e-6f) {
		return false;
	}
	direction = Normalize(direction);

	// 狙いのばらつき(角度で散らす)。
	if (aimSpreadDeg_ > 0.0f) {
		std::uniform_real_distribution<float> spread(-aimSpreadDeg_, aimSpreadDeg_);
		const float radians = std::numbers::pi_v<float> / 180.0f;
		const Vector3 side = Normalize(Cross({0.0f, 1.0f, 0.0f}, direction));
		const Vector3 up = Cross(direction, side);
		direction = Normalize(direction + side * std::tan(spread(random_) * radians) + up * std::tan(spread(random_) * radians));
	}
	outDirection = direction;
	return true;
}

void WaterSprayComponent::UpdateStream(Stream& stream, float deltaTime, bool isActive, const Vector3& nozzle) {
	// 水弾を動かす。前の位置から今の位置までを球で調べ、Collider に当たったらそこで止めて消す(すり抜け防止)。
	// 水弾自身もトリガーの Collider を持つので、トリガーは調べない(水弾どうしで当たらないように)。
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	stream.hitHold -= deltaTime;
	int newestHit = -1;
	for (int index = 0; index < static_cast<int>(stream.drops.size()); ++index) {
		WaterDropComponent& data = *stream.drops[index].data;
		Vector3& position = stream.drops[index].object->GetTransform().translation_;
		const Vector3 previous = position;
		Vector3 velocity = data.GetVelocity();
		velocity.y -= gravity_ * deltaTime;
		position += velocity * deltaTime;
		data.SetVelocity(velocity);
		data.SetAge(data.GetAge() + deltaTime);

		SphereCastHit hit;
		if (scene && hitRadius_ > 0.0f && SphereCast(*scene, previous, position, hitRadius_, hit, owner, false)) {
			position = hit.point;
			data.SetHit(true);
			if (particles_) {
				// 当たった面から跳ね返る向きへ散らす(来た向きの逆+少し上)。
				particles_->EmitAt(hit.point, Normalize(velocity * -1.0f) + Vector3{0.0f, 0.6f, 0.0f}, splashCount_);
			}
			if (hitFlash_) {
				StartFlash(hit.gameObject);
			}
			newestHit = index;
			stream.hitPoint = hit.point;
			stream.hitHold = kHitHoldTime;
			hasLastHit_ = true;
			lastHitPoint_ = hit.point;
		}
	}
	// 当たった水弾より古い水弾は、もう障害物の向こう側。水流としてつなぐと障害物を突き抜けて見えるので消す。
	for (int index = 0; index < newestHit; ++index) {
		stream.drops[index].data->SetHit(true);
	}

	// 消える水弾からしぶきを出す: 水面に落ちたものは上へ跳ね、空中で寿命が尽きたものは少し散る。
	for (const Drop& drop : stream.drops) {
		const Vector3& position = drop.object->GetTransform().translation_;
		const bool hit = drop.data->IsHit();
		// 海(OceanComponent)の上なら、その場所の海面が水面。海が無ければ Kill Height。
		float surface = killHeight_;
		if (scene) {
			OceanComponent::TryGetSurfaceHeight(*scene, position.x, position.z, surface);
		}
		const bool fell = position.y < surface;
		const bool expired = drop.data->GetAge() >= lifetime_;
		if (particles_ && !hit) {
			if (fell) {
				particles_->EmitAt({position.x, surface, position.z}, {0.0f, 1.0f, 0.0f}, splashCount_);
			} else if (expired) {
				particles_->EmitAt(position, drop.data->GetVelocity(), 1, drop.data->GetVelocity() * dropletInherit_);
			}
		}
		if (hit || fell || expired) {
			ReleaseDrop(drop);
		}
	}
	std::erase_if(stream.drops, [](const Drop& drop) { return !drop.object->IsActive(); });

	// 水流に沿って、ところどころから小さなしぶきを飛ばす(水弾の勢いを少し引き継いで、流れから外れていく)。
	if (particles_ && !stream.drops.empty() && dropletRate_ > 0.0f) {
		dropletAccumulator_ += dropletRate_ * deltaTime;
		std::uniform_int_distribution<size_t> pick(0, stream.drops.size() - 1);
		while (dropletAccumulator_ >= 1.0f) {
			dropletAccumulator_ -= 1.0f;
			const Drop& drop = stream.drops[pick(random_)];
			particles_->EmitAt(drop.object->GetTransform().translation_, drop.data->GetVelocity(), 1, drop.data->GetVelocity() * dropletInherit_);
		}
	}

	// 描く点は「新しい順」(先頭がノズル側)。出している最中の水流は、ノズルそのものも先頭の点にしてつなげる。
	points_.clear();
	widths_.clear();
	if (isActive) {
		points_.push_back(nozzle);
		widths_.push_back(1.0f);
	}
	for (auto it = stream.drops.rbegin(); it != stream.drops.rend(); ++it) {
		points_.push_back(it->object->GetTransform().translation_);
		// 先へ行く(古い)ほど水が広がって太くなり、消える直前は細くなる。
		const float age = it->data->GetAge();
		const float fade = std::clamp((lifetime_ - age) / (lifetime_ * 0.25f), 0.0f, 1.0f);
		widths_.push_back((1.0f + age * spreadGrowth_) * fade);
	}
	// 当たり続けている間は、当たった点を先端にする(太さは最後の水弾と同じ)。
	const bool tipAttached = stream.hitHold > 0.0f && !points_.empty();
	if (tipAttached) {
		points_.push_back(stream.hitPoint);
		widths_.push_back(widths_.back());
	}
	stream.spline->SetPoints(points_, &widths_);
	// 先端が物に当たって止まっている水流は、シェーダーに先端を欠けさせない(当たった所に穴が空いて見えるため)。
	stream.spline->SetShaderUserValue(tipAttached ? 1.0f : 0.0f);
}

void WaterSprayComponent::Update() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	EnsureStreams();
	if (streams_.empty()) {
		return;
	}
	EnsureDropFolders();
	const float deltaTime = Time::GetDeltaTime();
	owner->UpdateWorldTransformSelfAndAncestors();
	const Vector3 nozzle = owner->GetTransform().GetWorldPosition();
	hasLastHit_ = false;

	// 出し始めたら、次の入れ物で新しい水流を始める(前の水流は途切れたまま飛んでいく)。
	firing_ = ShouldFire();
	if (firing_) {
		if (activeStream_ < 0) {
			activeStream_ = nextStream_;
			nextStream_ = (nextStream_ + 1) % static_cast<int>(streams_.size());
			Stream& stream = streams_[activeStream_];
			for (const Drop& drop : stream.drops) {
				ReleaseDrop(drop);
			}
			stream.drops.clear();
			emitTimer_ = 0.0f;
		}
		// 一定の間隔で水弾を撃ち出す(フレームの速さに関係なく同じ密度になるように)。
		Vector3 direction{};
		if (GetAimDirection(nozzle, direction)) {
			emitTimer_ += deltaTime;
			while (emitTimer_ >= emitInterval_) {
				emitTimer_ -= emitInterval_;
				// 同じフレームで複数出すときは、出た時刻の差だけ先へ進めておく(粒が1か所に重ならないように)。
				const Vector3 velocity = direction * speed_;
				SpawnDrop(streams_[activeStream_], nozzle + velocity * emitTimer_, velocity, emitTimer_);
			}
		}
	} else {
		activeStream_ = -1;
	}

	for (int index = 0; index < static_cast<int>(streams_.size()); ++index) {
		// 見た目の設定は毎フレーム渡す(Inspector で変えるとすぐ反映される)。
		ApplyLook(*streams_[index].spline);
		UpdateStream(streams_[index], deltaTime, index == activeStream_, nozzle);
	}
	UpdateFlashes(deltaTime);
}

void WaterSprayComponent::StartFlash(GameObject* target) {
	ModelRendererComponent* renderer = target ? target->GetComponent<ModelRendererComponent>() : nullptr;
	if (!renderer) {
		return;
	}
	// 光る時間と、次に光れるまでの間隔。当て続けると、この間隔で点滅する。
	constexpr float kOnTime = 0.05f;
	constexpr float kInterval = 0.16f;
	// 明るさは1未満にする(1を超えるとブルームで大きくにじみ、当て続けるとまぶしくなる)。
	constexpr float kIntensity = 0.7f;
	for (Flash& flash : flashes_) {
		if (flash.renderer == renderer) {
			if (flash.cooldown <= 0.0f) {
				renderer->SetEmissiveOverride({1.0f, 1.0f, 1.0f}, kIntensity);
				flash.onTime = kOnTime;
				flash.cooldown = kInterval;
			}
			return;
		}
	}
	renderer->SetEmissiveOverride({1.0f, 1.0f, 1.0f}, kIntensity);
	flashes_.push_back({renderer, kOnTime, kInterval});
}

void WaterSprayComponent::UpdateFlashes(float deltaTime) {
	for (Flash& flash : flashes_) {
		const bool wasOn = flash.onTime > 0.0f;
		flash.onTime -= deltaTime;
		flash.cooldown -= deltaTime;
		if (wasOn && flash.onTime <= 0.0f) {
			flash.renderer->ClearEmissiveOverride();
		}
	}
	// 光り終わって、次に光れるようになったら忘れる(次に当たったらすぐ光る)。
	std::erase_if(flashes_, [](const Flash& flash) { return flash.onTime <= 0.0f && flash.cooldown <= 0.0f; });
}

} // namespace KujataEngine
