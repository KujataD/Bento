#include "ReviveGaugeUpdater.h"

#include "components/ImageComponent.h"
#include "PlayerHealth.h"

#include <Editor/PrefabAsset.h>

#include <cmath>

using namespace KujataEngine;

namespace {

GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object) {
		return nullptr;
	}
	for (GameObject* child : object->GetChildren()) {
		if (!child) {
			continue;
		}
		if (child->GetName() == name) {
			return child;
		}
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

void ReviveGaugeUpdater::OnPlayStart() {
	health_ = GetComponent<PlayerHealth>();
	// Playインスタンスごとに作り直す(前回Playのポインタは無効)。
	gauge_ = nullptr;
	gaugeTried_ = false;
	fillImage_ = nullptr;
}

void ReviveGaugeUpdater::OnPlayStop() { gauge_ = nullptr; }

void ReviveGaugeUpdater::Update() {
	if (!owner_ || !health_) {
		return;
	}

	// 倒れていないときは何も出さない。**器を作るのも倒れてからでよい**ので、
	// 生きている間はPrefabの読み込みすら起こさない。
	if (!health_->IsDead()) {
		HideGauge();
		return;
	}

	GameObject* gauge = AcquireGauge();
	if (!gauge) {
		return;
	}

	Vector3 anchor = owner_->GetTransform().translation_;
	anchor.y += height_;

	WorldTransform& transform = gauge->GetTransform();
	transform.translation_ = anchor;

	// World Space Canvas は自分ではカメラを向かないので、Yawだけこちらで向ける。
	if (owner_->GetScene()) {
		if (GameObject* camera = owner_->GetScene()->FindGameObjectByName(cameraName_)) {
			Vector3 toCamera = camera->GetTransform().translation_ - anchor;
			if (std::fabs(toCamera.x) > 0.0001f || std::fabs(toCamera.z) > 0.0001f) {
				transform.rotation_.y = std::atan2(toCamera.x, toCamera.z);
			}
		}
	}

	if (fillImage_) {
		fillImage_->SetFillAmount(health_->GetReviveProgress());
	}
	gauge->SetActive(true);
}

GameObject* ReviveGaugeUpdater::AcquireGauge() {
	if (gauge_) {
		return gauge_;
	}
	if (gaugeTried_ || !owner_ || !owner_->GetScene() || gaugePrefabPath_.empty()) {
		return nullptr;
	}

	// **一度だけ試す。** 失敗するたびに読み直すとUpdate中に生成を繰り返して重くなる。
	gaugeTried_ = true;
	PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*owner_->GetScene(), gaugePrefabPath_, false);
	if (!result.succeeded || !result.rootObject) {
		Logger::Log("[ReviveGaugeUpdater] prefab load failed (" + gaugePrefabPath_ + "): " + result.message);
		return nullptr;
	}
	gauge_ = result.rootObject;
	if (GameObject* fill = FindDescendantByName(gauge_, fillObjectName_)) {
		fillImage_ = fill->GetComponent<ImageComponent>();
	}
	return gauge_;
}

void ReviveGaugeUpdater::HideGauge() {
	if (gauge_ && gauge_->IsActive()) {
		gauge_->SetActive(false);
	}
}
