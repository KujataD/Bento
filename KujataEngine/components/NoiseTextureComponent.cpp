#include "NoiseTextureComponent.h"
#include "../assets/NoiseTexture.h"
#include "../scene/GameObject.h"
#include "ModelRendererComponent.h"
#include "ParticleSystemComponent.h"
#include <algorithm>

namespace KujataEngine {

void NoiseTextureComponent::Initialize() { RegenerateAndApply(); }

void NoiseTextureComponent::RegenerateAndApply() {
	if (!GetOwner()) {
		return;
	}

	uint32_t size = static_cast<uint32_t>((std::max)(textureSize_, 1));

	if (!IsModelKind(kind_)) {
		const uint32_t textureIndex = (kind_ == static_cast<int>(Kind::FlameSprite)) ? NoiseTexture::GenerateFlameSpriteTexture(size, seedLane_)
		                                                                            : NoiseTexture::GenerateDustSpriteTexture(size, seedLane_);
		if (ParticleSystemComponent* particles = GetComponent<ParticleSystemComponent>()) {
			particles->SetTextureOverride(textureIndex);
		}
		return;
	}

	uint32_t textureIndex = 0;
	switch (static_cast<Kind>(kind_)) {
	case Kind::SoilAlbedo:
		textureIndex = NoiseTexture::GenerateSoilAlbedoTexture(size, seedLane_);
		break;
	case Kind::MossyRock:
		textureIndex = NoiseTexture::GenerateMossyRockTexture(size, seedLane_);
		break;
	case Kind::Sand:
		textureIndex = NoiseTexture::GenerateSandTexture(size, seedLane_);
		break;
	case Kind::RustedMetal:
		textureIndex = NoiseTexture::GenerateRustedMetalTexture(size, seedLane_);
		break;
	case Kind::CrackedRock:
		textureIndex = NoiseTexture::GenerateCrackedRockTexture(size, seedLane_);
		break;
	default:
		textureIndex = NoiseTexture::GenerateRockAlbedoTexture(size, seedLane_);
		break;
	}

	if (ModelRendererComponent* renderer = GetComponent<ModelRendererComponent>()) {
		renderer->SetTextureOverride(textureIndex);
		// ワールド座標で敷き詰める。CB側は「1ユニットあたりの繰り返し数」なので逆数を渡す。
		const float tileSize = (tileSize_ > 0.001f) ? tileSize_ : 0.001f;
		renderer->SetTriplanarScaleOverride(1.0f / tileSize);
		// 白のときは触らない。既定値で上書きすると、Materialで色を付けている既存オブジェクトの
		// Base Colorを白へ潰してしまう(このコンポーネントを足しただけで色が変わるのは事故になる)。
		if (tint_.x != 1.0f || tint_.y != 1.0f || tint_.z != 1.0f || tint_.w != 1.0f) {
			renderer->SetColorOverride(tint_);
		}
	}
}

void NoiseTextureComponent::RegisterInvokableMethods(InvokableMethodRegistry& registry) { registry.Add("Regenerate", [this]() { RegenerateAndApply(); }); }

} // namespace KujataEngine
