#pragma once

#include "../math/Matrix4x4.h"
#include "../runtime/KujataApi.h"

#include "UIRect.h"
#include <vector>

namespace KujataEngine {

class Camera;
class Scene;
class GameObject;

/// <summary>
/// Scene内の1つのUI要素(RectTransformを持つGameObject)の、RTピクセル空間での矩形。
/// UI編集ビューでのクリック選択・整列ガイドに使う。out は描画順(後=手前)で並ぶ。
/// </summary>
struct UIElementRectInfo {
	GameObject* object = nullptr;
	GameObject* canvasObject = nullptr;
	UIRect pixelRect; // RTピクセル空間(canvas単位 * scaleFactor)
	float scaleFactor = 1.0f;
};

/// <summary>
/// Scene内の全Canvas配下のUI要素の準備(テクスチャ/フォントの読み込み)を行う。
/// コマンドリスト実行を伴うため、必ず描画パスの外(PrepareFrame)で呼ぶこと。
/// </summary>
KUJATA_API void PrepareSceneCanvases(Scene& scene);

/// <summary>
/// Scene内の全Canvasを走査し、スクリーン空間UIを描画する。
/// 3D描画の後(RenderViewの末尾)に呼ぶこと。targetWidth/Heightは描画先RTのピクセルサイズ。
/// </summary>
KUJATA_API void DrawSceneCanvases(Scene& scene, float targetWidth, float targetHeight);

/// <summary>
/// Scene内のWorld Space CanvasをGameObjectのTransform位置へ描画する。
/// スクリーン空間UIと違い3Dオブジェクトに遮蔽されるため、3D描画・スプライト描画の後に呼ぶこと。
/// </summary>
KUJATA_API void DrawSceneWorldCanvases(Scene& scene, Camera* camera, float targetWidth, float targetHeight);

/// <summary>
/// World Space Canvasの「キャンバス単位 → Canvasローカル」変換行列。
/// UIは左上原点・Y下方向、worldはY上方向なので、中心寄せしてYを反転する。
/// 描画と入力(レイキャスト)で同じ変換を使うために共有する。
/// </summary>
KUJATA_API Matrix4x4 MakeCanvasUIToLocalMatrix(float canvasWidth, float canvasHeight);

/// <summary>
/// 指定UI要素(RectTransformを持つGameObject)の矩形をキャンバス単位で算出する。
/// Canvas祖先が無い/RectTransformが欠ける場合はfalse。outScaleFactorはキャンバス→ピクセル倍率。
/// エディタのSceneビュー矩形ギズモ用。
/// </summary>
KUJATA_API bool ComputeUIElementRect(GameObject* node, float targetWidth, float targetHeight, UIRect& outRect, float& outScaleFactor);

/// <summary>
/// objectがUI関連(Canvas自身、またはCanvasの子孫のUI要素)かを判定する。
/// Sceneビューで「UIを選択中のときだけUIを表示する」判定に使う。
/// </summary>
KUJATA_API bool IsUIRelatedObject(GameObject* object);

/// <summary>
/// Scene内の全Canvas配下のUI要素を走査し、各要素のRTピクセル空間矩形を描画順(後=手前)で集める。
/// UI編集ビューのクリック選択・整列スナップに使う。targetWidth/HeightはRTのピクセルサイズ。
/// </summary>
KUJATA_API void CollectSceneUIElementRects(Scene& scene, float targetWidth, float targetHeight, std::vector<UIElementRectInfo>& out);

} // namespace KujataEngine
