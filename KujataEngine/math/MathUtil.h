#pragma once
#include <assert.h>
#include <cmath>
#include "../runtime/KujataApi.h"
#include <math/Matrix3x3.h>
#include <math/Matrix4x4.h>
#include <math/Vector2.h>
#include <math/Vector3.h>
#include <math/Vector4.h>
#include <math/Random.h>
#include <numbers>


namespace KujataEngine {
class Segment;

// --- vector2 ---

KUJATA_API float Distance(const Vector2& v1, const Vector2& v2);

// --- vector3 ---

inline Vector3 operator+(Vector3 v, float f) { return {v.x + f, v.y + f, v.z + f}; }
inline Vector3 operator-(Vector3 v, float f) { return {v.x - f, v.y - f, v.z - f}; }
inline Vector3 operator*(Vector3 v, float f) { return {v.x * f, v.y * f, v.z * f}; }
inline Vector3 operator/(Vector3 v, float f) { return {v.x / f, v.y / f, v.z / f}; }
inline Vector3 operator+(float f, Vector3 v) { return {v.x + f, v.y + f, v.z + f}; }
inline Vector3 operator-(float f, Vector3 v) { return {v.x - f, v.y - f, v.z - f}; }
inline Vector3 operator*(float f, Vector3 v) { return {v.x * f, v.y * f, v.z * f}; }

KUJATA_API Vector3 Transform(const Vector3& v, const Matrix4x4& m);

KUJATA_API float Dot(const Vector3& v1, const Vector3& v2);
KUJATA_API Vector3 Cross(const Vector3& a, const Vector3& b);
KUJATA_API float Length(const Vector3& v);
KUJATA_API Vector3 Normalize(const Vector3& v);
KUJATA_API float Lerp(float f1, float f2, float t);
KUJATA_API Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);
KUJATA_API Vector3 Slerp(const Vector3& v1, const Vector3& v2, float t);
KUJATA_API Vector3 Bezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, float t);
KUJATA_API Vector3 Reflect(const Vector3& input, const Vector3& normal);
KUJATA_API Vector3 Project(const Vector3& a, const Vector3& b);
KUJATA_API Vector3 Project(const Vector3& worldPosition, float viewportX, float viewportY, float viewportWidth, float viewportHeight, const Matrix4x4& matView, const Matrix4x4& matProjection);

KUJATA_API Vector3 ClosestPoint(const Vector3& point, const Segment& segment);


KUJATA_API Vector3 Perpendicular(const Vector3& vector);
KUJATA_API Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m);
KUJATA_API Vector3 LookAt(const Vector3& velocity);
KUJATA_API float WrapAngle(float angle);
KUJATA_API float LerpShortAngle(float a, float b, float t);
KUJATA_API Vector3 Limit(Vector3 v, float max);

// --- matrix4x4 ---

KUJATA_API Matrix4x4 MakeAffineMatrixOrientations(const Vector3 orientations[3], const Vector3& translate);

KUJATA_API Matrix4x4 Inverse(const Matrix4x4& m);
KUJATA_API Matrix4x4 Transpose(const Matrix4x4& m);
KUJATA_API Matrix4x4 MakeIdentity();

KUJATA_API Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
KUJATA_API Matrix4x4 MakeScaleMatrix(const Vector3& scale);
KUJATA_API Matrix4x4 MakeRotateXMatrix(float radian);
KUJATA_API Matrix4x4 MakeRotateYMatrix(float radian);
KUJATA_API Matrix4x4 MakeRotateZMatrix(float radian);
KUJATA_API Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
KUJATA_API Matrix4x4 MakeRotateMatrix( const Vector3& rotate);

// 透視投影行列
KUJATA_API Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

// 正射影行列
KUJATA_API Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

// ビューポート変換行列
KUJATA_API Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

KUJATA_API TransformationMatrix MakeBillboardMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate, const class Camera& camera);

inline Matrix4x4 kBackToFrontMatrix = MakeRotateYMatrix(std::numbers::pi_v<float>);

} // namespace KujataEngine
