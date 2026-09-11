#include "HPBarUpdater.h"
#include "EnemyHealth.h"

void HPBarUpdater::Initialize() {
	AcquireRefs();
	ApplyHealthToBar();
}

void HPBarUpdater::Update() {
	// コールバックに頼らず毎フレームHP率をバーへ反映する(確実に追従させる)。
	// Initialize時に子(HPBarFill)が未構築でも、ここで拾い直す。
	AcquireRefs();
	ApplyHealthToBar();
}

void HPBarUpdater::AcquireRefs() {
	KujataEngine::GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	if (!health_) {
		health_ = owner->GetComponent<EnemyHealth>();
	}

	if (!hpBarFill_) {
		// HPBarFill を探す（Enemy直下の子オブジェクト）
		for (KujataEngine::GameObject* child : owner->GetChildren()) {
			if (child && child->GetName() == "HPBarFill") {
				hpBarFill_ = child;
				break;
			}
		}
	}

	if (hpBarFill_ && !baseCaptured_) {
		// JSONで設定した初期スケール/位置を満タン時の基準として一度だけ記憶する。
		KujataEngine::WorldTransform& transform = hpBarFill_->GetTransform();
		baseScaleX_ = transform.scale_.x;
		basePosX_ = transform.translation_.x;
		baseCaptured_ = true;
	}
}

void HPBarUpdater::ApplyHealthToBar() {
	if (!hpBarFill_ || !health_) {
		return;
	}

	float healthPercent = health_->GetHealthPercent();

	KujataEngine::WorldTransform& transform = hpBarFill_->GetTransform();
	// 基準スケールにHP率を掛けて横方向に縮める(背景と同じ基準幅を維持)。
	transform.scale_.x = baseScaleX_ * healthPercent;

	// バー左端を固定したまま右から減らす。
	transform.translation_.x = basePosX_ - baseScaleX_ * (1.0f - healthPercent);
}
