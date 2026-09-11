#include "LightObjectFactory.h"

#include "../components/DirectionalLightComponent.h"
#include "../components/PointLightComponent.h"
#include "../components/SpotLightComponent.h"
#include "../math/MathUtil.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

namespace KujataEngine {
namespace LightObjectFactory {

namespace {

template <class T>
T* CreateLightObject(Scene* scene, const char* name, GameObject** outObject) {
	*outObject = nullptr;
	if (!scene) {
		return nullptr;
	}

	GameObject* object = scene->CreateGameObject(name);
	if (!object) {
		return nullptr;
	}
	*outObject = object;

	T* light = object->AddComponent<T>();
	if (light) {
		// ビルボードアイコンの読み込みなどScene側の初期化を通す。
		scene->OnEditorComponentAdded(object, light);
	}
	return light;
}

} // namespace

GameObject* CreateDirectionalLight(Scene* scene) {
	GameObject* object = nullptr;
	DirectionalLightComponent* light = CreateLightObject<DirectionalLightComponent>(scene, "Directional Light", &object);
	if (light) {
		// Unityの既定(Rotation 50, -30, 0)と同じ向き。真上からより陰影が出る。
		light->GetData().direction = Normalize({-0.32f, -0.77f, 0.55f});
		light->Apply();
	}
	if (object) {
		// 平行光源は位置に意味が無いが、原点だと他と重なるので少し上げておく。
		object->GetTransform().translation_ = {0.0f, 3.0f, 0.0f};
	}
	return object;
}

GameObject* CreatePointLight(Scene* scene) {
	GameObject* object = nullptr;
	PointLightComponent* light = CreateLightObject<PointLightComponent>(scene, "Point Light", &object);
	if (light) {
		// PointLightDataの既定はradius 10 / intensity 1。Unityの既定Rangeと同じなのでそのまま使う。
		light->GetData().intensity = 1.0f;
	}
	if (object) {
		object->GetTransform().translation_ = {0.0f, 3.0f, 0.0f};
	}
	return object;
}

GameObject* CreateSpotLight(Scene* scene) {
	GameObject* object = nullptr;
	CreateLightObject<SpotLightComponent>(scene, "Spot Light", &object);
	if (object) {
		// 既定の+Z前方だと真横を向いて地面を照らさないため、Unityが薦める配置と同じく
		// 上から見下ろす角度にしておく(X+90度で真下向き)。
		object->GetTransform().translation_ = {0.0f, 5.0f, 0.0f};
		object->GetTransform().rotation_ = {1.5707964f, 0.0f, 0.0f};
	}
	return object;
}

} // namespace LightObjectFactory
} // namespace KujataEngine
