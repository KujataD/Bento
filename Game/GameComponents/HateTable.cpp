#include "HateTable.h"

#include "PlayerHealth.h"

#include <algorithm>
#include <cmath>

using namespace KujataEngine;

void HateTable::OnPlayStart() {
	Clear();
}

void HateTable::Clear() {
	entries_.clear();
	target_ = nullptr;
}

HateTable::Entry* HateTable::Find(const GameObject* source) {
	for (Entry& entry : entries_) {
		if (entry.source == source) {
			return &entry;
		}
	}
	return nullptr;
}

float HateTable::GetHate(const GameObject* source) const {
	for (const Entry& entry : entries_) {
		if (entry.source == source) {
			return entry.hate;
		}
	}
	return 0.0f;
}

void HateTable::AddHate(GameObject* source, float amount) {
	if (!source || amount == 0.0f) {
		return;
	}
	if (Entry* entry = Find(source)) {
		entry->hate += amount;
		if (entry->hate < 0.0f) {
			entry->hate = 0.0f;
		}
		return;
	}
	entries_.push_back(Entry{source, (std::max)(amount, 0.0f)});
}

void HateTable::AddDamageHate(GameObject* source, float damage) {
	AddHate(source, damage * damageHateScale_);
}

void HateTable::GatherCandidates(std::vector<GameObject*>& outCandidates) const {
	outCandidates.clear();
	if (!owner_ || !owner_->GetScene()) {
		return;
	}
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy() || object->GetTag() != targetTag_) {
			continue;
		}
		// 死んだ相手は候補から外す(倒した側へ自動的に狙いが移る)。
		PlayerHealth* health = object->GetComponent<PlayerHealth>();
		if (!health || !health->IsAlive()) {
			continue;
		}
		outCandidates.push_back(object.get());
	}
}

void HateTable::DropMissing(const std::vector<GameObject*>& candidates) {
	// **ポインタの比較しかしない。** 死亡や破棄で候補から消えた相手の実体を触ると
	// 解放済みメモリを読むことになるため、ここでは中身を一切参照しない。
	entries_.erase(
	    std::remove_if(entries_.begin(), entries_.end(),
	        [&candidates](const Entry& entry) {
		        return std::find(candidates.begin(), candidates.end(), entry.source) == candidates.end();
	        }),
	    entries_.end());

	if (target_ && std::find(candidates.begin(), candidates.end(), target_) == candidates.end()) {
		target_ = nullptr;
	}
}

void HateTable::Update() {
	if (!owner_) {
		return;
	}
	float deltaTime = Time::GetDeltaTime();
	if (deltaTime <= 0.0f) {
		return;
	}

	std::vector<GameObject*> candidates;
	GatherCandidates(candidates);
	DropMissing(candidates);

	if (candidates.empty()) {
		target_ = nullptr;
		return;
	}

	const Vector3 selfPosition = owner_->GetTransform().translation_;

	// --- 1. 距離によるヘイト。近いほど強く、Proximity Rangeで0になる ---
	if (proximityHatePerSecond_ > 0.0f && proximityRange_ > 0.0f) {
		for (GameObject* candidate : candidates) {
			Vector3 diff = candidate->GetTransform().translation_ - selfPosition;
			diff.y = 0.0f;
			float distance = std::sqrt(diff.x * diff.x + diff.z * diff.z);
			float nearness = 1.0f - std::clamp(distance / proximityRange_, 0.0f, 1.0f);
			if (nearness > 0.0f) {
				AddHate(candidate, proximityHatePerSecond_ * nearness * deltaTime);
			}
		}
	}

	// --- 2. 時間減衰。割合で減らすので、大きい値ほど速く落ちて差が縮まる ---
	if (decayPerSecond_ > 0.0f) {
		float keep = std::pow(1.0f - std::clamp(decayPerSecond_, 0.0f, 0.999f), deltaTime);
		for (Entry& entry : entries_) {
			entry.hate *= keep;
		}
	}

	// --- 3. ターゲット決定 ---
	// 射程外(Lose Target Distance超)は候補から外す。0なら無制限。
	auto withinLeash = [&](GameObject* object) {
		if (loseTargetDistance_ <= 0.0f) {
			return true;
		}
		Vector3 diff = object->GetTransform().translation_ - selfPosition;
		diff.y = 0.0f;
		return std::sqrt(diff.x * diff.x + diff.z * diff.z) <= loseTargetDistance_;
	};

	GameObject* best = nullptr;
	float bestHate = 0.0f;
	for (const Entry& entry : entries_) {
		if (!withinLeash(entry.source)) {
			continue;
		}
		if (!best || entry.hate > bestHate) {
			best = entry.source;
			bestHate = entry.hate;
		}
	}

	if (!best) {
		// ヘイトが付いている相手が全員射程外。近い方へ切り替えて戦闘を続ける。
		float nearestSq = 0.0f;
		for (GameObject* candidate : candidates) {
			Vector3 diff = candidate->GetTransform().translation_ - selfPosition;
			diff.y = 0.0f;
			float distanceSq = diff.x * diff.x + diff.z * diff.z;
			if (!best || distanceSq < nearestSq) {
				best = candidate;
				nearestSq = distanceSq;
			}
		}
		target_ = best;
		return;
	}

	if (!target_ || target_ == best) {
		target_ = best;
		return;
	}

	// **乗り換えにはヒステリシスを噛ませる。**
	// 単純な最大値選択だと、2人のヘイトが並んだ瞬間に毎フレーム狙いが入れ替わり、
	// 旋回と移動が細かく往復して見た目が壊れる。
	float currentHate = GetHate(target_);
	if (bestHate > currentHate * switchRatio_) {
		target_ = best;
	}
}
