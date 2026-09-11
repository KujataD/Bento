#pragma once
#include "../runtime/KujataApi.h"

#include "ShapeUtil.h"
#include "../3d/Camera.h"
#include "../vfx/InstancingModel.h"
#include <vector>

namespace KujataEngine {
namespace ShapeUtil {

// 制御点をCatmull-Romスプラインで補間し、その上にビルボードパーティクルを並べて描画する。
// InstancingModel(GPU)に依存するため、純粋な衝突判定(ShapeUtil.h)からは分離している。
KUJATA_API void DrawSplineParticles(InstancingModel* model, const std::vector<Vector3>& controlPoints, const Camera& camera);

} // namespace ShapeUtil
} // namespace KujataEngine
