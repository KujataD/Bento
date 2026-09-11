#include "Vector2.h"
#include "Matrix3x3.h"
#include <cassert>
namespace KujataEngine{

Vector2::Vector2() : x(0.0f), y(0.0f) {}
Vector2::Vector2(float x, float y) : x(x), y(y) {}

float Vector2::Length(const Vector2& v) { return sqrtf(v.x * v.x + v.y * v.y); }

Vector2 Vector2::Normalize(const Vector2& v) {
	float length = Length(v);

	// ベクトルの長さが0の場合、正規化できないので、(0, 0)を返す
	if (length == 0.0f) {
		return {0.0f, 0.0f};
	}

	// ベクトルを正規化
	return {v.x / length, v.y / length};
}

float Vector2::Dot(const Vector2& v1, const Vector2& v2) { return v1.x * v2.x + v1.y * v2.y; }

float Vector2::Orientation(const Vector2& a, const Vector2& b, const Vector2& p) {
	Vector2 ap = p - a;
	Vector2 ab = b - a;

	return ap.x * ab.y - ap.y * ab.x;
}

Vector2 Vector2::operator+(float f) const { return {x + f, y + f}; }

Vector2 Vector2::operator+(const Vector2& other) const { return {x + other.x, y + other.y}; }

Vector2& Vector2::operator+=(float f) {
	x += f;
	y += f;
	return *this;
}
Vector2& Vector2::operator+=(const Vector2& other) {
	x += other.x;
	y += other.y;
	return *this;
}

Vector2 Vector2::operator-(float f) const { return {x - f, y - f}; }

Vector2 Vector2::operator-(const Vector2& other) const { return {x - other.x, y - other.y}; }

Vector2& Vector2::operator-=(float f) {
	x -= f;
	y -= f;
	return *this;
}

Vector2& Vector2::operator-=(const Vector2& other) {
	x -= other.x;
	y -= other.y;
	return *this;
}

Vector2 Vector2::operator*(float f) { return {x * f, y * f}; }

Vector2& Vector2::operator*=(float f) {
	x *= f;
	y *= f;
	return *this;
}

Vector2 Vector2::operator*(const Vector2& other) const { return {x * other.x, y * other.y}; }

Vector2& Vector2::operator*=(const Vector2& other) {
	x *= other.x;
	y *= other.y;
	return *this;
}

Vector2 Vector2::operator/(float f) { return {x / f, y / f}; }

Vector2& Vector2::operator/=(float f) {
	x /= f;
	y /= f;
	return *this;
}

Vector2 Vector2::operator/(const Vector2& other) const { return {x / other.x, y / other.y}; }

Vector2& Vector2::operator/=(const Vector2& other) {
	x /= other.x;
	y /= other.y;
	return *this;
}

bool Vector2::operator!=(const Vector2& other) const {
	return (x != other.x || y != other.y);
}

bool Vector2::operator==(const Vector2& other) const {
	return (x == other.x && y == other.y);
}


Vector2 Vector2::Transform(const Matrix3x3& m) const {
	Vector2 result;
	result.x = x * m.m[0][0] + y * m.m[1][0] + 1.0f * m.m[2][0];
	result.y = x * m.m[0][1] + y * m.m[1][1] + 1.0f * m.m[2][1];
	float w = x * m.m[0][2] + y * m.m[1][2] + 1.0f * m.m[2][2];
	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	return result;
}
}