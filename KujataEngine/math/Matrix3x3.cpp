#include "Matrix3x3.h"
#include <cassert>
#include <cmath>

namespace KujataEngine {

Matrix3x3::Matrix3x3() {
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m[i][j] = 0.0f;
		}
	}
}

Matrix3x3::Matrix3x3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22) {
	m[0][0] = m00;
	m[0][1] = m01;
	m[0][2] = m02;
	m[1][0] = m10;
	m[1][1] = m11;
	m[1][2] = m12;
	m[2][0] = m20;
	m[2][1] = m21;
	m[2][2] = m22;
}

Matrix3x3 Matrix3x3::operator+(float f) const {
	Matrix3x3 result(*this);

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result.m[i][j] += f;
		}
	}

	return result;
}

Matrix3x3 Matrix3x3::operator-(float f) const {
	Matrix3x3 result(*this);

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result.m[i][j] -= f;
		}
	}
	return result;
}

Matrix3x3 Matrix3x3::operator*(float f) const {
	Matrix3x3 result(*this);

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result.m[i][j] *= f;
		}
	}

	return result;
}

Matrix3x3 Matrix3x3::operator/(float f) const {
	Matrix3x3 result(*this);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result.m[i][j] /= f;
		}
	}
	return result;
}

Matrix3x3 Matrix3x3::operator*(const Matrix3x3& other) const {
	Matrix3x3 result;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result.m[i][j] = m[i][0] * other.m[0][j] + m[i][1] * other.m[1][j] + m[i][2] * other.m[2][j];
		}
	}
	return result;
}

Matrix3x3 Matrix3x3::MakeScaleMatrix(const Vector2& scale) { return {scale.x, 0.0f, 0.0f, 0.0f, scale.y, 0.0f, 0.0f, 0.0f, 1.0f}; }

Matrix3x3 Matrix3x3::MakeRotateMatrix(float theta) { return {cosf(theta), sinf(theta), 0.0f, -sinf(theta), cosf(theta), 0.0f, 0.0f, 0.0f, 1.0f}; }

Matrix3x3 Matrix3x3::MakeTranslateMatrix(const Vector2& translate) { return {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, translate.x, translate.y, 1.0f}; }

Matrix3x3 Matrix3x3::MakeAffineMatrix(const Vector2& scale, float rotate, const Vector2& translate) { return MakeScaleMatrix(scale) * MakeRotateMatrix(rotate) * MakeTranslateMatrix(translate); }

Matrix3x3 Matrix3x3::MakeOrthographicMatrix(float left, float top, float right, float bottom) {
	return {2.0f / (right - left), 0.0f, 0.0f, 0.0f, 2.0f / (top - bottom), 0.0f, (left + right) / (left - right), (top + bottom) / (bottom - top), 1.0f};
}

Matrix3x3 Matrix3x3::MakeViewportMatrix(float left, float top, float width, float height) {
	return {width / 2.0f, 0.0f, 0.0f, 0.0f, -(height / 2.0f), 0.0f, left + (width / 2.0f), top + (height / 2.0f), 1.0f};
}

Matrix3x3 Matrix3x3::Inverse(const Matrix3x3& m) {
	Matrix3x3 result(
	    m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1], -(m.m[0][1] * m.m[2][2] - m.m[0][2] * m.m[2][1]), m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1],

	    -(m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]), m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0], -(m.m[0][0] * m.m[1][2] - m.m[0][2] * m.m[1][0]),

	    m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0], -(m.m[0][0] * m.m[2][1] - m.m[0][1] * m.m[2][0]), m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0]);

	float det = m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] + m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][2] * m.m[1][1] * m.m[2][0] - m.m[0][1] * m.m[1][0] * m.m[2][2] -
	            m.m[0][0] * m.m[1][2] * m.m[2][1];

	return result / det;
}

Matrix3x3 Matrix3x3::Transpose(const Matrix3x3& m) { return {m.m[0][0], m.m[1][0], m.m[2][0], m.m[0][1], m.m[1][1], m.m[2][1], m.m[0][2], m.m[1][2], m.m[2][2]}; }
} // namespace KujataEngine