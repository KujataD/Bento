#include "BuiltinComponents.h"
#include "AnimatorComponent.h"
#include "AudioSourceComponent.h"
#include "ButtonComponent.h"
#include "CameraComponent.h"
#include "CanvasComponent.h"
#include "ColliderComponent.h"
#include "DebugCameraComponent.h"
#include "DirectionalLightComponent.h"
#include "ImageComponent.h"
#include "ModelRendererComponent.h"
#include "NoiseTextureComponent.h"
#include "ParticleSystemComponent.h"
#include "TrailRendererComponent.h"
#include "OrbitCameraComponent.h"
#include "PointLightComponent.h"
#include "RectTransformComponent.h"
#include "SpotLightComponent.h"
#include "RigidbodyComponent.h"
#include "SpriteRendererComponent.h"
#include "TextComponent.h"
#include "RotatorComponent.h"
#include "TransformComponent.h"
#include "VolumeComponent.h"
#include "../scene/ComponentFactory.h"

namespace KujataEngine {

void RegisterBuiltinComponents() {
	static bool registered = false;
	if (registered) {
		return;
	}

	ComponentFactory& factory = ComponentFactory::GetInstance();
	factory.RegisterComponent<TransformComponent>();
	factory.RegisterComponent<RotatorComponent>();
	factory.RegisterComponent<ModelRendererComponent>();
	factory.RegisterComponent<NoiseTextureComponent>();
	factory.RegisterComponent<ParticleSystemComponent>();
	factory.RegisterComponent<TrailRendererComponent>();
	// world空間2D(Sprite方式)。スクリーン空間UIはCanvas方式のImageComponent側。
	factory.RegisterComponent<SpriteRendererComponent>();
	factory.RegisterComponent<CameraComponent>();
	factory.RegisterComponent<DebugCameraComponent>();
	factory.RegisterComponent<OrbitCameraComponent>();
	factory.RegisterComponent<DirectionalLightComponent>();
	factory.RegisterComponent<PointLightComponent>();
	factory.RegisterComponent<SpotLightComponent>();
	factory.RegisterComponent<RigidbodyComponent>();
	factory.RegisterComponent<AnimatorComponent>();
	factory.RegisterComponent<AudioSourceComponent>();
	factory.RegisterComponent<SphereColliderComponent>();
	factory.RegisterComponent<BoxColliderComponent>();
	factory.RegisterComponent<CapsuleColliderComponent>();
	factory.RegisterComponent<CanvasComponent>();
	factory.RegisterComponent<RectTransformComponent>();
	factory.RegisterComponent<ImageComponent>();
	factory.RegisterComponent<TextComponent>();
	factory.RegisterComponent<ButtonComponent>();
	// ポストエフェクト(Fog/Bloom等)をシーンへ配置するためのVolume。
	factory.RegisterComponent<VolumeComponent>();

	registered = true;
}

} // namespace KujataEngine
