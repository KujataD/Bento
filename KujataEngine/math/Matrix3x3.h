#pragma once
#include "../runtime/KujataApi.h"
#include "Vector2.h"


namespace KujataEngine{

class KUJATA_API Matrix3x3 {
public:
	float m[3][3];

	Matrix3x3();
	Matrix3x3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22);

	// 演算子オーバーロード
	Matrix3x3 operator+(float f) const;
	Matrix3x3 operator-(float f) const;
	Matrix3x3 operator*(float f) const;
	Matrix3x3 operator/(float f) const;
	Matrix3x3 operator*(const Matrix3x3& other) const;

	// 行列生成
	static Matrix3x3 MakeScaleMatrix(const Vector2& scale);
	static Matrix3x3 MakeRotateMatrix(float theta);
	static Matrix3x3 MakeTranslateMatrix(const Vector2& translate);
	static Matrix3x3 MakeAffineMatrix(const Vector2& scale, float rotate, const Vector2& translate);
	static Matrix3x3 MakeOrthographicMatrix(float left, float top, float right, float bottom);
	static Matrix3x3 MakeViewportMatrix(float left, float top, float width, float height);

	// その他
	static Matrix3x3 Inverse(const Matrix3x3& m);
	static Matrix3x3 Transpose(const Matrix3x3& m);
};
}