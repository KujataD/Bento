#pragma once

namespace KujataEngine {

class Scene;
class GameObject;

// GameObjectメニューとHierarchy右クリックで共有するUI要素生成。
// いずれもCanvas(無ければ生成)配下に作る。
namespace UIObjectFactory {

/// <summary>
/// Screen Space - OverlayのCanvasを**常に新規作成**する。
/// Canvasは複数置ける(sortOrderで前後を制御)ので、作成メニューはこちらを使う。
/// </summary>
GameObject* CreateCanvas(Scene* scene);

/// <summary>
/// World Space Canvasを**常に新規作成**する。
/// ワールドに置くUIなので、Transformのscaleをキャンバス単位→world単位の倍率として初期設定する。
/// </summary>
GameObject* CreateWorldSpaceCanvas(Scene* scene);

/// <summary>
/// UI要素の親にするCanvasを決める。既存が無ければ作る。
/// 選択中のGameObjectがいずれかのCanvas配下なら**そのCanvas**を返すので、
/// Canvasが複数あっても「今見ているCanvas」に追加される(Unityと同じ挙動)。
/// </summary>
GameObject* EnsureCanvas(Scene* scene);

GameObject* CreateImage(Scene* scene);
GameObject* CreateText(Scene* scene);
GameObject* CreateButton(Scene* scene);

} // namespace UIObjectFactory
} // namespace KujataEngine
