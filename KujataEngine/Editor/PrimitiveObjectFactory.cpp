#include "PrimitiveObjectFactory.h"

#include "../components/ColliderComponent.h"
#include "../components/ModelRendererComponent.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

namespace KujataEngine {
namespace PrimitiveObjectFactory {

namespace {

// 既定テクスチャは白1x1。MaterialのBase Colorだけで見た目を作れる状態から始める。
constexpr const char* kDefaultTexturePath = "Resources/white1x1.png";

/// GameObject + ModelRendererまでの共通部分。Colliderは形ごとに寸法が違うので呼び出し側で足す。
GameObject* CreatePrimitiveObject(Scene* scene, const char* name, ModelRendererComponent::PrimitiveType primitive) {
	if (!scene) {
		return nullptr;
	}

	GameObject* object = scene->CreateGameObject(name);
	if (!object) {
		return nullptr;
	}

	ModelRendererComponent* renderer = object->AddComponent<ModelRendererComponent>();
	if (renderer) {
		renderer->SetPrimitive(primitive, kDefaultTexturePath);
		// 描画カメラの割り当てなどScene側の初期化を通す(SampleSceneがここでカメラを差す)。
		scene->OnEditorComponentAdded(object, renderer);
	}
	return object;
}

template <class T>
T* AddCollider(Scene* scene, GameObject* object) {
	T* collider = object->AddComponent<T>();
	if (collider) {
		scene->OnEditorComponentAdded(object, collider);
	}
	return collider;
}

} // namespace

GameObject* CreateCube(Scene* scene) {
	GameObject* object = CreatePrimitiveObject(scene, "Cube", ModelRendererComponent::PrimitiveType::Cube);
	if (!object) {
		return nullptr;
	}
	// Cubeメッシュは±0.5の1辺1。BoxColliderの既定サイズ(1,1,1)がそのまま一致する。
	AddCollider<BoxColliderComponent>(scene, object);
	return object;
}

GameObject* CreateSphere(Scene* scene) {
	GameObject* object = CreatePrimitiveObject(scene, "Sphere", ModelRendererComponent::PrimitiveType::Sphere);
	if (!object) {
		return nullptr;
	}
	// Sphereメッシュは半径1の単位球。SphereColliderの既定は0.5なので合わせる。
	if (SphereColliderComponent* collider = AddCollider<SphereColliderComponent>(scene, object)) {
		collider->SetRadius(1.0f);
	}
	return object;
}

GameObject* CreateCapsule(Scene* scene) {
	GameObject* object = CreatePrimitiveObject(scene, "Capsule", ModelRendererComponent::PrimitiveType::Capsule);
	if (!object) {
		return nullptr;
	}
	// Capsuleメッシュは radius=0.5 / height=2.0 / Y軸(Model::CreateCapsuleの既定)。
	// CapsuleColliderの既定値と同じなので追加設定は要らない。
	AddCollider<CapsuleColliderComponent>(scene, object);
	return object;
}

} // namespace PrimitiveObjectFactory
} // namespace KujataEngine
