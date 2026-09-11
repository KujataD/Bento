#pragma once

#include "../runtime/KujataApi.h"

namespace KujataEngine {

class Camera;
class Scene;

/// <summary>
/// world空間2Dスプライト(Sprite方式)の描画パス。
/// 3D描画の後・UI(Canvas)描画の前に呼ぶこと。
/// </summary>
KUJATA_API void DrawSceneSprites(Scene& scene, Camera* camera);

} // namespace KujataEngine
