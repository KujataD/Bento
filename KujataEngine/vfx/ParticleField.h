#pragma once
#include "../math/Matrix4x4.h"
#include "../shapes/ShapeUtil.h"

namespace KujataEngine {

struct AccelerationField {
	Vector3 acceleration; // !< 加速度
	AABB area;            // !< 範囲
};

} // namespace KujataEngine