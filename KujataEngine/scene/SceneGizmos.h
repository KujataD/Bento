#pragma once

#include "../math/Vector4.h"

namespace KujataEngine {

class Scene;

/// <summary>
/// Sceneビューに線で描く編集用ギズモ(Colliderのワイヤー、カメラの視錐台、ライトの影響範囲、グリッド)。
/// どれもLineRendererへ線を積むだけ。どのビューで描くかは呼び出し側(Scene::RenderView)が決める。
/// </summary>
namespace SceneGizmos {

/// <summary>
/// 選択中のGameObjectとその配下のColliderを描く。
/// </summary>
void DrawSelectedColliders(const Scene& scene);

/// <summary>
/// シーン内の全アクティブGameObjectのColliderを描く(ゲーム中の当たり判定確認用)。
/// </summary>
void DrawAllColliders(const Scene& scene);

/// <summary>
/// 選択中のGameObjectがカメラを持つ場合、その視錐台を描く。
/// </summary>
void DrawSelectedCameraFrustum(const Scene& scene);

/// <summary>
/// 選択中のGameObjectが持つライトの影響範囲(PointLightは球、SpotLightはコーン)を描く。
/// </summary>
void DrawSelectedLightRanges(const Scene& scene);

/// <summary>
/// XZ平面のグリッドを描く。
/// </summary>
void DrawGrid(const Vector4& color);

/// <summary>
/// 全Colliderを描くデバッグモードのフラグ(Scene全体で共有)。
/// </summary>
bool IsShowAllColliders();
void SetShowAllColliders(bool show);

} // namespace SceneGizmos

} // namespace KujataEngine
