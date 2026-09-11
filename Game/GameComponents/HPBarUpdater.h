#pragma once

#include <KujataEngine.h>

class EnemyHealth;

class HPBarUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "HPBarUpdater"; }

	void Initialize() override;
	void Update() override;

private:
	KujataEngine::GameObject* hpBarFill_ = nullptr;
	EnemyHealth* health_ = nullptr;

	// 満タン時のバー基準値(JSONで設定した初期スケール/位置)。HP率を掛けて縮める。
	float baseScaleX_ = 1.0f;
	float basePosX_ = 0.0f;
	bool baseCaptured_ = false;

	// health_/hpBarFill_を未取得なら解決する。Initialize時に子が未構築でもUpdateで拾い直す。
	void AcquireRefs();
	void ApplyHealthToBar();
};
