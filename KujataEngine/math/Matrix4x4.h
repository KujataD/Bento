#pragma once
#include "../runtime/KujataApi.h"
#include "Vector3.h"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <numbers>

namespace KujataEngine {
struct TransformationMatrix;
class KUJATA_API Matrix4x4 {
public:
	float m[4][4];

	Matrix4x4 operator+(const Matrix4x4& m) const;
	Matrix4x4 operator-(const Matrix4x4& m) const;
	Matrix4x4 operator*(const Matrix4x4& m) const;
	Matrix4x4 operator/(float scalar) const;
};
} // namespace KujataEngine