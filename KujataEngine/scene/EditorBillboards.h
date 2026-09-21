#pragma once

namespace KujataEngine {

class Scene;
class Component;

/// <summary>
/// Sceneビューに出すComponentのアイコン(IEditorBillboardを実装したComponentの位置に置く板ポリ)。
/// テクスチャ読み込みはCommandListを実行するため、Prepare系は描画パス外で呼ぶこと。
/// Drawは準備済みのものだけを描く。
/// </summary>
namespace EditorBillboards {

/// <summary>
/// 1つのComponentのアイコンを準備する(IEditorBillboardでなければ何もしない)。冪等。
/// </summary>
void PrepareComponent(const Component* component);

/// <summary>
/// Scene内の全Componentのアイコンを準備する。冪等。
/// </summary>
void PrepareScene(Scene& scene);

/// <summary>
/// SceneのEditorカメラで準備済みのアイコンを描き、消えたComponentの分のキャッシュを捨てる。
/// </summary>
void Draw(Scene& scene);

/// <summary>
/// キャッシュ(モデルと変換)を全部捨てる。Scene破棄時に呼ぶ。
/// </summary>
void ClearCache();

} // namespace EditorBillboards

} // namespace KujataEngine
