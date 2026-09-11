#include "GameFx.h"

#include <Editor/PrefabAsset.h>

#include <unordered_map>

using namespace KujataEngine;

namespace GameFx {

namespace {

/// <summary>Prefabごとの使い回しプール。**Playインスタンス単位**なので、Play開始時に必ず捨てる。</summary>
std::unordered_map<std::string, std::vector<GameObject*>>& Pools() {
	static std::unordered_map<std::string, std::vector<GameObject*>> pools;
	return pools;
}

/// <summary>
/// 休眠中の器を拾う。無ければPrefabから作る。
/// **粒が消え切ったものだけを再利用する。** まだ粒が残っている器を使い回すと、
/// 前の土埃が途中で瞬間移動して不自然になる。
/// </summary>
GameObject* Acquire(Scene* scene, const std::string& prefabPath) {
	if (!scene || prefabPath.empty()) {
		return nullptr;
	}

	std::vector<GameObject*>& pool = Pools()[prefabPath];
	for (GameObject* pooled : pool) {
		if (!pooled) {
			continue;
		}
		ParticleSystemComponent* system = pooled->GetComponent<ParticleSystemComponent>();
		if (system && system->GetAliveCount() == 0 && !system->IsEmitting()) {
			return pooled;
		}
	}

	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, prefabPath, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[GameFx] prefab load failed (" + prefabPath + "): " + result.message);
		return nullptr;
	}
	pool.push_back(result.rootObject);
	return result.rootObject;
}

} // namespace

void ResetPools() { Pools().clear(); }

GameObject* Burst(Scene* scene, const std::string& prefabPath, const Vector3& position, float strength, const Vector4* color) {
	GameObject* object = Acquire(scene, prefabPath);
	if (!object) {
		return nullptr;
	}

	object->GetTransform().translation_ = position;
	object->SetActive(true);

	ParticleSystemComponent* system = object->GetComponent<ParticleSystemComponent>();
	if (!system) {
		return object;
	}
	if (color) {
		system->SetColorOverride(*color);
	} else {
		system->ClearColorOverride();
	}
	system->SetStrength(strength);
	// ワンショットなので持続発生は止めたまま、1回ぶんだけ出す。
	system->SetEmitting(false);
	system->Burst();
	return object;
}

GameObject* Ignite(Scene* scene, const std::string& prefabPath, const Vector3& position, const Vector4* color) {
	GameObject* object = Acquire(scene, prefabPath);
	if (!object) {
		return nullptr;
	}

	object->GetTransform().translation_ = position;
	object->SetActive(true);

	ParticleSystemComponent* system = object->GetComponent<ParticleSystemComponent>();
	if (!system) {
		return object;
	}
	if (color) {
		system->SetColorOverride(*color);
	} else {
		system->ClearColorOverride();
	}
	system->SetStrength(1.0f);
	system->SetEmitting(true);
	return object;
}

void Extinguish(GameObject* fxObject) {
	if (!fxObject) {
		return;
	}
	if (ParticleSystemComponent* system = fxObject->GetComponent<ParticleSystemComponent>()) {
		// **その場で消さない。** 発生だけ止めて、残っている粒は寿命どおりに消えさせる。
		system->SetEmitting(false);
	}
}

} // namespace GameFx
