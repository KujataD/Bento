#pragma once

namespace KujataEngine {

class Scene;
class ColliderComponent;
struct Contact;

/// <summary>
/// Rigidbodyの物理(重力つき速度積分と、衝突時の押し出し+速度反射)。
/// 衝突の検出とイベント通知はSceneCollisionSystemが受け持ち、ここは剛体の応答だけを扱う。
/// </summary>
namespace ScenePhysics {

/// <summary>
/// Rigidbodyの速度を位置へ積分する。動的ボディには重力(gravityScale倍)を加え、Freeze Position軸を固定する。
/// キネマティック(Is Static)は重力・拘束を受けず、自身の速度でそのまま移動する。
/// </summary>
void IntegrateRigidbodies(Scene& scene, float deltaTime);

/// <summary>
/// 非トリガーの交差ペアに剛体反発(押し出し+速度反射)を適用する。
/// contactはcolliderA→colliderB基準。
/// </summary>
void ResolveCollisionResponse(ColliderComponent* colliderA, ColliderComponent* colliderB, const Contact& contact);

} // namespace ScenePhysics

} // namespace KujataEngine
