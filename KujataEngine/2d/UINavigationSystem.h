#pragma once

#include "../runtime/KujataApi.h"

namespace KujataEngine {

class Scene;

/// <summary>
/// ゲームパッド/キーボードによるUIフォーカス操作(UnityのEventSystemのNavigation相当)を1フレーム分進める。
/// UIEventSystemから呼ばれ、ポインタ操作と排他で動く(UIInputModeを参照/更新する)。
///
/// 操作:
///   移動   : 左スティック / 十字キー / 矢印キー / WASD(押しっぱなしでリピート)
///   決定   : Aボタン / Enter / Space
///   キャンセル: Bボタン / Esc / BackSpace → フォーカス中CanvasのOn Cancelを発火
///
/// 対象は「Screen Space - OverlayのCanvasのうち、操作可能なボタンを持つ最前面(sortOrder最大)の1枚」だけ。
/// こうすることで、HUDの上にポーズ/死亡メニューを重ねたときフォーカスが背後へ漏れない(モーダルになる)。
/// World Space Canvasはキャンバスをまたいだ位置比較ができないためナビゲーションの対象外。
/// </summary>
/// <returns>
/// フォーカス操作がこのフレームのUIを担当したらtrue。
/// falseならポインタ(マウス)側が従来どおり処理する。
/// </returns>
KUJATA_API bool UpdateUINavigation(Scene& scene, float targetWidth, float targetHeight);

} // namespace KujataEngine
