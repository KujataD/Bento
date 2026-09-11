#pragma once
#include "../runtime/KujataApi.h"
#include "Camera.h"
#include "WorldTransform.h"

namespace KujataEngine {

class KUJATA_API FollowCamera {
public:
	void Initialize();

	void Update();

	const Camera& GetCamera() const { return viewProjection_; }
	void SetTarget(const WorldTransform* target) { target_ = target; }

private:
	Camera viewProjection_;
	const WorldTransform* target_ = nullptr;
};

} // namespace KujataEngine
