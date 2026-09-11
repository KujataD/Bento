#pragma once

namespace KujataEngine {

class Scene;
class GameObject;

// Hierarchyの Create > Light で共有するライト生成。
// 名前・既定値はUnityのGameObject > Lightに合わせてある。
namespace LightObjectFactory {

/// 平行光源。向きはDirectionalLightComponentのDirectionで指定する。
GameObject* CreateDirectionalLight(Scene* scene);

/// 点光源。Radiusの球がSceneビューにギズモとして出る。
GameObject* CreatePointLight(Scene* scene);

/// スポットライト。Transformの前方(+Z)へ照射し、コーンのギズモが出る。
GameObject* CreateSpotLight(Scene* scene);

} // namespace LightObjectFactory
} // namespace KujataEngine
