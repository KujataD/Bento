#pragma once
#include <assert.h>
#include <cmath>
#include <numbers>

namespace KujataEngine {

class Matrix4x4;
class Segment;
class Vector3 {
public:
	float x;
	float y;
	float z;

	Vector3 operator+(const Vector3& v) const { return {x + v.x, y + v.y, z + v.z}; }
	Vector3 operator-(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }
	Vector3 operator-() const { return {-x, -y, -z}; }

	void operator+=(const Vector3& v) {
		x += v.x;
		y += v.y;
		z += v.z;
	}
	void operator-=(const Vector3& v) {
		x -= v.x;
		y -= v.y;
		z -= v.z;
	}
	void operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
	}
	void operator/=(float scalar) {
		x /= scalar;
		y /= scalar;
		z /= scalar;
	}
};

} // namespace KujataEngine