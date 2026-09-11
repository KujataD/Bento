#pragma once
#include "../runtime/KujataApi.h"
#include <cmath>
#include <math.h>

namespace KujataEngine{

class Matrix3x3;

class KUJATA_API Vector2 {
public:
	float x;
	float y;

	Vector2();
	Vector2(float x, float y);

	// 便利関数
	// ----------------------------------------------------
	static float Length(const Vector2& v);
	static Vector2 Normalize(const Vector2& v);
	static float Dot(const Vector2& a, const Vector2& b);
	static float Orientation(const Vector2& a, const Vector2& b, const Vector2& p);

	// 演算子オーバーロード
	// ----------------------------------------------------
	Vector2 operator+(float f) const;
	Vector2 operator+(const Vector2& other) const;
	Vector2& operator+=(float f);
	Vector2& operator+=(const Vector2& other);
	Vector2 operator-(float f) const;
	Vector2 operator-(const Vector2& other) const;
	Vector2& operator-=(float f);
	Vector2& operator-=(const Vector2& other);
	Vector2 operator*(float f);
	Vector2& operator*=(float f);
	Vector2 operator*(const Vector2& other) const;
	Vector2& operator*=(const Vector2& other);
	Vector2 operator/(float f);
	Vector2& operator/=(float f);
	Vector2 operator/(const Vector2& other) const;
	Vector2& operator/=(const Vector2& other);

	bool operator!=(const Vector2& other) const;
	bool operator==(const Vector2& other) const;

	Vector2 Transform(const Matrix3x3& m) const;
};

}
