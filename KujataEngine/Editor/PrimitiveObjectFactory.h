#pragma once

namespace KujataEngine {

class Scene;
class GameObject;

// Hierarchyの Create > 3D で共有する3Dプリミティブ生成。
// いずれもModelRendererComponentと、その形状に合わせたColliderを付けた状態で返す。
// Colliderの寸法はメッシュの実寸(Model::CreateXxx)と一致させてあるので、
// 生成直後からギズモとメッシュがぴったり重なる。
namespace PrimitiveObjectFactory {

/// 1辺1の立方体 + BoxCollider(1, 1, 1)。
GameObject* CreateCube(Scene* scene);

/// 半径1の球 + SphereCollider(radius 1)。
GameObject* CreateSphere(Scene* scene);

/// radius 0.5 / height 2.0 / Y軸のカプセル + 同寸法のCapsuleCollider。
GameObject* CreateCapsule(Scene* scene);

} // namespace PrimitiveObjectFactory
} // namespace KujataEngine
