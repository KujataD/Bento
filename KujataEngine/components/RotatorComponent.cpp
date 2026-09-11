#include "RotatorComponent.h"
#include "../scene/GameObject.h"
#include <cmath>

namespace KujataEngine {

void RotatorComponent::Update() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	owner->GetTransform().rotation_.y += speed_;
}

} // namespace KujataEngine
