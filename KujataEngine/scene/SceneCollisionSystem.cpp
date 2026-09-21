#include "SceneCollisionSystem.h"
#include "Scene.h"
#include "ScenePhysics.h"
#include "../components/ColliderComponent.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KujataEngine {

namespace {

enum class CollisionEventPhase {
	Enter,
	Stay,
	Exit,
};

std::string MakeCollisionPairKey(const ColliderComponent& colliderA, const ColliderComponent& colliderB) {
	uint64_t idA = colliderA.GetRuntimeId();
	uint64_t idB = colliderB.GetRuntimeId();
	if (idA > idB) {
		std::swap(idA, idB);
	}

	return std::to_string(idA) + ":" + std::to_string(idB);
}

// contact は colliderA→colliderB の接触情報(非トリガーで交差中のみ非null)。
// flipNormal=true のとき normal を反転して self 視点(self→other)に揃える。
void NotifyCollisionToComponents(GameObject* owner, ColliderComponent* self, ColliderComponent* other, bool isTrigger, CollisionEventPhase phase, const Contact* contact, bool flipNormal) {
	if (!owner || !self || !other) {
		return;
	}

	for (const std::unique_ptr<Component>& component : owner->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		if (isTrigger) {
			if (phase == CollisionEventPhase::Enter) {
				component->OnTriggerEnter(other);
			} else if (phase == CollisionEventPhase::Stay) {
				component->OnTriggerStay(other);
			} else if (phase == CollisionEventPhase::Exit) {
				component->OnTriggerExit(other);
			}
			continue;
		}

		Collision collision{};
		collision.self = self;
		collision.other = other;
		collision.selfObject = owner;
		collision.otherObject = other->GetOwner();
		if (contact) {
			collision.normal = flipNormal ? contact->normal * -1.0f : contact->normal;
			collision.penetrationDepth = contact->depth;
			collision.point = contact->point;
		} else {
			collision.point = self->GetWorldCenter();
		}

		if (phase == CollisionEventPhase::Enter) {
			component->OnCollisionEnter(collision);
		} else if (phase == CollisionEventPhase::Stay) {
			component->OnCollisionStay(collision);
		} else if (phase == CollisionEventPhase::Exit) {
			component->OnCollisionExit(collision);
		}
	}
}

void NotifyCollisionPair(ColliderComponent* colliderA, ColliderComponent* colliderB, bool isTrigger, CollisionEventPhase phase, const Contact* contact = nullptr) {
	if (!colliderA || !colliderB) {
		return;
	}

	// contact は colliderA→colliderB 基準。B側へは normal を反転して渡す。
	NotifyCollisionToComponents(colliderA->GetOwner(), colliderA, colliderB, isTrigger, phase, contact, false);
	NotifyCollisionToComponents(colliderB->GetOwner(), colliderB, colliderA, isTrigger, phase, contact, true);
}

void CollectSceneColliders(Scene& scene, std::vector<ColliderComponent*>& outColliders) {
	outColliders.clear();

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}

			ColliderComponent* collider = component->AsColliderComponent();
			if (!collider) {
				continue;
			}

			outColliders.push_back(collider);
		}
	}
}

} // namespace

void SceneCollisionSystem::Update(Scene& scene) {
	std::vector<ColliderComponent*> colliders;
	CollectSceneColliders(scene, colliders);

	std::unordered_set<ColliderComponent*> activeColliders;
	for (ColliderComponent* collider : colliders) {
		activeColliders.insert(collider);
	}

	// ブロードフェーズ(Sweep and Prune): X軸のAABB minでソートし、Xレンジが重ならない組は
	// 候補から外す。真に交差するペアは全軸でAABBが重なっているはずなので、X軸だけ見ても
	// 取りこぼしは起きない(候補が減るだけで、絞り込みすぎることはない)。
	// コライダー数がN個ある今の規模(ガーディアン等ボーンだらけの敵込みで100前後)では、
	// 総当たり(N^2)よりペア数を大きく減らせる。
	struct BroadPhaseEntry {
		size_t originalIndex;
		AABB aabb;
	};
	std::vector<BroadPhaseEntry> entries;
	entries.reserve(colliders.size());
	for (size_t index = 0; index < colliders.size(); ++index) {
		entries.push_back({index, colliders[index]->GetWorldAABB()});
	}
	std::sort(entries.begin(), entries.end(), [](const BroadPhaseEntry& lhs, const BroadPhaseEntry& rhs) { return lhs.aabb.min.x < rhs.aabb.min.x; });

	std::unordered_map<std::string, PairState> currentPairStates;

	std::vector<BroadPhaseEntry> active;
	for (const BroadPhaseEntry& entry : entries) {
		// activeのうち、今回のentryより手前でXレンジが終わっている(=もう重なりようがない)ものを外す。
		active.erase(std::remove_if(active.begin(), active.end(),
		                 [&entry](const BroadPhaseEntry& candidate) { return candidate.aabb.max.x < entry.aabb.min.x; }),
		    active.end());

		for (const BroadPhaseEntry& other : active) {
			// 元のcolliders配列でのindexが小さい方をA、大きい方をBに揃える
			// (Sweep順ではなく元の並びで固定し、接触法線の向き等の既存挙動を変えない)。
			size_t indexA = (std::min)(entry.originalIndex, other.originalIndex);
			size_t indexB = (std::max)(entry.originalIndex, other.originalIndex);
			ColliderComponent* colliderA = colliders[indexA];
			ColliderComponent* colliderB = colliders[indexB];

			GameObject* ownerA = colliderA->GetOwner();
			GameObject* ownerB = colliderB->GetOwner();
			if (ownerA == ownerB) {
				continue;
			}
			// 同じ階層ルート(=同じキャラクター)に属するコライダー同士は既定では判定しない。
			// ガーディアンの脚ボーンのように部位ごとにコライダーを分けた構成で、
			// 自分自身の部位同士が毎フレーム無意味に交差判定されるのを防ぐ
			// (絵にもゲームプレイにも出ない負荷。ボーン数が増えるほどO(n^2)で効いてくる)。
			if (ownerA && ownerB && ownerA->GetHierarchyRoot() == ownerB->GetHierarchyRoot()) {
				continue;
			}
			if (!colliderA->Intersects(*colliderB)) {
				continue;
			}

			bool isTrigger = colliderA->IsTrigger() || colliderB->IsTrigger();

			// 非トリガーは接触情報(A→B)を計算し、物理応答(押し出し+速度反射)を適用する。
			// 同じ接触情報をCollisionイベントのnormal/penetration充填にも使う。
			Contact contact{};
			bool hasContact = false;
			if (!isTrigger) {
				hasContact = colliderA->ComputeContact(*colliderB, contact);
				if (hasContact) {
					ScenePhysics::ResolveCollisionResponse(colliderA, colliderB, contact);
				}
			}
			const Contact* contactPtr = hasContact ? &contact : nullptr;

			std::string pairKey = MakeCollisionPairKey(*colliderA, *colliderB);
			currentPairStates[pairKey] = {colliderA, colliderB, isTrigger};

			auto previous = pairStates_.find(pairKey);
			if (previous == pairStates_.end()) {
				NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Enter, contactPtr);
				continue;
			}

			if (previous->second.isTrigger != isTrigger) {
				NotifyCollisionPair(previous->second.colliderA, previous->second.colliderB, previous->second.isTrigger, CollisionEventPhase::Exit);
				NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Enter, contactPtr);
				continue;
			}

			NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Stay, contactPtr);
		}

		active.push_back(entry);
	}

	for (const auto& [pairKey, previousState] : pairStates_) {
		if (currentPairStates.contains(pairKey)) {
			continue;
		}
		if (!activeColliders.contains(previousState.colliderA) || !activeColliders.contains(previousState.colliderB)) {
			continue;
		}

		NotifyCollisionPair(previousState.colliderA, previousState.colliderB, previousState.isTrigger, CollisionEventPhase::Exit);
	}

	pairStates_ = std::move(currentPairStates);
}

} // namespace KujataEngine
