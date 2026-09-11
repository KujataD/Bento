#include "StaminaComponent.h"
#include <algorithm>

using namespace KujataEngine;

void StaminaComponent::Initialize() {
	stamina_ = maxStamina_;
	regenDelayTimer_ = 0.0f;
}

void StaminaComponent::OnPlayStart() {
	stamina_ = maxStamina_;
	regenDelayTimer_ = 0.0f;
}

void StaminaComponent::Update() {
	float deltaTime = Time::GetDeltaTime();

	if (regenDelayTimer_ > 0.0f) {
		regenDelayTimer_ -= deltaTime;
		if (regenDelayTimer_ > 0.0f) {
			return;
		}
		regenDelayTimer_ = 0.0f;
	}

	if (stamina_ < maxStamina_) {
		stamina_ = std::min(maxStamina_, stamina_ + regenPerSecond_ * deltaTime);
	}
}

bool StaminaComponent::Consume(float amount) {
	if (amount <= 0.0f) {
		return false;
	}
	bool hadStamina = stamina_ > 0.0f;
	stamina_ = std::max(0.0f, stamina_ - amount);
	regenDelayTimer_ = regenDelay_;
	return hadStamina && stamina_ <= 0.0f;
}

bool StaminaComponent::Drain(float perSecond) {
	Consume(perSecond * Time::GetDeltaTime());
	return stamina_ > 0.0f;
}
