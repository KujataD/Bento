#pragma once

#include "../runtime/KujataApi.h"

namespace KujataEngine {

class Camera;
class Scene;

/// <summary>
/// UIのポインタ入力処理。Canvasを走査してポインタ下の最前面Buttonを判定し、
/// ホバー/押下の見た目更新とクリック時のonClick発火を行う。描画パス外(Update)で呼ぶこと。
/// </summary>
/// <param name="camera">
/// World Space Canvasの判定に使うカメラ(ポインタからレイを飛ばす)。
/// nullptrならScreen Space - Overlayだけを処理する。
/// </param>
KUJATA_API void UpdateUIEventSystem(Scene& scene, float targetWidth, float targetHeight, Camera* camera = nullptr);

} // namespace KujataEngine
