#include "MathUtil.h"
#include "../3d/Camera.h"
#include "../3d/WorldTransform.h"
#include <shapes/ShapeUtil.h>

namespace KujataEngine {
float Distance(const Vector2& v1, const Vector2& v2) {
	const float dx = v1.x - v2.x;
	const float dy = v1.y - v2.y;
	return std::sqrt(dx * dx + dy * dy);
}

float Dot(const Vector3& v1, const Vector3& v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(const Vector3& v) { return std::sqrt(Dot(v, v)); }
Vector3 Normalize(const Vector3& v) {
	float len = Length(v);

	if (len == 0.0f) {
		return Vector3{0.0f, 0.0f, 0.0f};
	}

	return v / len;
}

float Lerp(float f1, float f2, float t) { return f1 + (f2 - f1) * t; }

Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t) {
	return {
	    v1.x + (v2.x - v1.x) * t,
	    v1.y + (v2.y - v1.y) * t,
	    v1.z + (v2.z - v1.z) * t,
	};
}

Vector3 Slerp(const Vector3& a, const Vector3& b, float t) {
	Vector3 na = Normalize(a);
	Vector3 nb = Normalize(b);

	// cosθを求める
	float dot = Dot(na, nb);

	// 範囲外阻止
	dot = std::clamp(dot, -1.0f, 1.0f);

	// na->nbの角度θ
	float theta = std::acos(dot);

	// 角度がほぼ0（同じ方向）ならLerpで代用
	if (theta < 0.0001f) {
		return Normalize(Lerp(na, nb, t));
	}

	float sinTheta = std::sin(theta);

	// naのウェイト
	float w1 = std::sin((1.0f - t) * theta) / sinTheta;
	// nbのウェイト
	float w2 = std::sin(t * theta) / sinTheta;

	return na * w1 + nb * w2;
}

Vector3 Bezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, float t) {
	Vector3 a = Lerp(p0, p1, t);
	Vector3 b = Lerp(p1, p2, t);
	return Lerp(a, b, t);
}

Vector3 Reflect(const Vector3& input, const Vector3& normal) { return input - normal * (2.0f * Dot(input, normal)); }

Vector3 Transform(const Vector3& v, const Matrix4x4& m) {
	Vector3 result;
	result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + 1.0f * m.m[3][0];
	result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + 1.0f * m.m[3][1];
	result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + 1.0f * m.m[3][2];
	float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + 1.0f * m.m[3][3];
	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

Vector3 Project(const Vector3& a, const Vector3& b) {
	float dotAB = Dot(a, b);
	float fieldNormB = std::powf(Length(b), 2);
	float dotNormAB = dotAB / fieldNormB;
	Vector3 result = b * dotNormAB;
	return result;
}

KujataEngine::Vector3 Project(const Vector3& worldPosition, float viewportX, float viewportY, float viewportWidth, float viewportHeight, const Matrix4x4& matView, const Matrix4x4& matProjection)
{
    Matrix4x4 matViewport = MakeViewportMatrix(
        viewportX, viewportY,
        viewportWidth, viewportHeight,
        0, 1);

    Matrix4x4 matVPV = matView * matProjection * matViewport;

    return Transform(worldPosition, matVPV);
}

Vector3 ClosestPoint(const Vector3& point, const Segment& segment) {
	Vector3 a = point - segment.origin;
	float t = Dot(a, segment.diff) / std::powf(Length(segment.diff), 2);
	t = std::clamp(t, 0.0f, 1.0f);
	Vector3 cp = segment.origin + segment.diff * t;
	return cp;
}

Vector3 Perpendicular(const Vector3& vector) {
	if (vector.x != 0.0f || vector.y != 0.0f) {
		return {-vector.y, vector.x, 0.0f};
	}
	return {0.0f, -vector.z, vector.y};
}

Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
	Vector3 result{v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0], v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1], v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]};

	return result;
}

Vector3 LookAt(const Vector3& velocity) {
	Vector3 rotation{};

	// Y軸周り角度(θy) ...atan2(高さ, 底辺)
	float targetY = std::atan2(velocity.x, velocity.z);
	// 横軸方向の長さを求める
	float velocityXZ = Length({velocity.x, 0.0f, velocity.z});
	// X軸周り角度(θx)
	float targetX = std::atan2(-velocity.y, velocityXZ);

	rotation.x = targetX;
	rotation.y = targetY;

	return rotation;
}

float WrapAngle(float angle) {

	while (angle > std::numbers::pi_v<float>) {
		angle -= std::numbers::pi_v<float> * 2.0f;
	}

	while (angle < -std::numbers::pi_v<float>) {
		angle += std::numbers::pi_v<float> * 2.0f;
	}

	return angle;
}

float LerpShortAngle(float a, float b, float t) {
	// 最短角度差
	float diff = WrapAngle(b - a);

	// 最短経路で補間
	return a + diff * t;
}

Vector3 Limit(Vector3 v, float max) {
	float length = Length(v);
	if (length > max) {
		Vector3 result = Normalize(v);
		result.x *= max;
		result.y *= max;
		result.z *= max;
		return result;
	}
	return v;
}

Matrix4x4 MakeAffineMatrixOrientations(const Vector3 orientations[3], const Vector3& translate) {
	return {
	    {{orientations[0].x, orientations[0].y, orientations[0].z, 0.0f},
	     {orientations[1].x, orientations[1].y, orientations[1].z, 0.0f},
	     {orientations[2].x, orientations[2].y, orientations[2].z, 0.0f},
	     {translate.x, translate.y, translate.z, 1.0f}}
    };
}
Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 result;

	result.m[0][0] = m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][3] * m.m[3][2] - m.m[1][2] * m.m[2][1] * m.m[3][3] -
	                 m.m[1][3] * m.m[2][2] * m.m[3][1];

	result.m[0][1] = -m.m[0][1] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][3] * m.m[3][2] +
	                 m.m[0][2] * m.m[2][1] * m.m[3][3] + m.m[0][3] * m.m[2][2] * m.m[3][1];

	result.m[0][2] = m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][3] * m.m[3][2] - m.m[0][2] * m.m[1][1] * m.m[3][3] -
	                 m.m[0][3] * m.m[1][2] * m.m[3][1];

	result.m[0][3] = -m.m[0][1] * m.m[1][2] * m.m[2][3] - m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][3] * m.m[2][2] +
	                 m.m[0][2] * m.m[1][1] * m.m[2][3] + m.m[0][3] * m.m[1][2] * m.m[2][1];

	result.m[1][0] = -m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[1][3] * m.m[2][0] * m.m[3][2] + m.m[1][0] * m.m[2][3] * m.m[3][2] +
	                 m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[1][3] * m.m[2][2] * m.m[3][0];

	result.m[1][1] = m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] + m.m[0][3] * m.m[2][0] * m.m[3][2] - m.m[0][0] * m.m[2][3] * m.m[3][2] - m.m[0][2] * m.m[2][0] * m.m[3][3] -
	                 m.m[0][3] * m.m[2][2] * m.m[3][0];

	result.m[1][2] = -m.m[0][0] * m.m[1][2] * m.m[3][3] - m.m[0][2] * m.m[1][3] * m.m[3][0] - m.m[0][3] * m.m[1][0] * m.m[3][2] + m.m[0][0] * m.m[1][3] * m.m[3][2] +
	                 m.m[0][2] * m.m[1][0] * m.m[3][3] + m.m[0][3] * m.m[1][2] * m.m[3][0];

	result.m[1][3] = m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] + m.m[0][3] * m.m[1][0] * m.m[2][2] - m.m[0][0] * m.m[1][3] * m.m[2][2] - m.m[0][2] * m.m[1][0] * m.m[2][3] -
	                 m.m[0][3] * m.m[1][2] * m.m[2][0];

	result.m[2][0] = m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][0] * m.m[2][3] * m.m[3][1] - m.m[1][1] * m.m[2][0] * m.m[3][3] -
	                 m.m[1][3] * m.m[2][1] * m.m[3][0];

	result.m[2][1] = -m.m[0][0] * m.m[2][1] * m.m[3][3] - m.m[0][1] * m.m[2][3] * m.m[3][0] - m.m[0][3] * m.m[2][0] * m.m[3][1] + m.m[0][0] * m.m[2][3] * m.m[3][1] +
	                 m.m[0][1] * m.m[2][0] * m.m[3][3] + m.m[0][3] * m.m[2][1] * m.m[3][0];

	result.m[2][2] = m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] + m.m[0][3] * m.m[1][0] * m.m[3][1] - m.m[0][0] * m.m[1][3] * m.m[3][1] - m.m[0][1] * m.m[1][0] * m.m[3][3] -
	                 m.m[0][3] * m.m[1][1] * m.m[3][0];

	result.m[2][3] = -m.m[0][0] * m.m[1][1] * m.m[2][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] - m.m[0][3] * m.m[1][0] * m.m[2][1] + m.m[0][0] * m.m[1][3] * m.m[2][1] +
	                 m.m[0][1] * m.m[1][0] * m.m[2][3] + m.m[0][3] * m.m[1][1] * m.m[2][0];

	result.m[3][0] = -m.m[1][0] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1] + m.m[1][0] * m.m[2][2] * m.m[3][1] +
	                 m.m[1][1] * m.m[2][0] * m.m[3][2] + m.m[1][2] * m.m[2][1] * m.m[3][0];

	result.m[3][1] = m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] + m.m[0][2] * m.m[2][0] * m.m[3][1] - m.m[0][0] * m.m[2][2] * m.m[3][1] - m.m[0][1] * m.m[2][0] * m.m[3][2] -
	                 m.m[0][2] * m.m[2][1] * m.m[3][0];

	result.m[3][2] = -m.m[0][0] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][2] * m.m[3][0] - m.m[0][2] * m.m[1][0] * m.m[3][1] + m.m[0][0] * m.m[1][2] * m.m[3][1] +
	                 m.m[0][1] * m.m[1][0] * m.m[3][2] + m.m[0][2] * m.m[1][1] * m.m[3][0];

	result.m[3][3] = m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] + m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][0] * m.m[1][2] * m.m[2][1] - m.m[0][1] * m.m[1][0] * m.m[2][2] -
	                 m.m[0][2] * m.m[1][1] * m.m[2][0];

	float det;
	det =
	    (m.m[0][0] * m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[0][0] * m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[0][0] * m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[0][0] * m.m[1][3] * m.m[2][2] * m.m[3][1] -
	     m.m[0][0] * m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[0][0] * m.m[1][1] * m.m[2][3] * m.m[3][2] - m.m[0][1] * m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[1][0] * m.m[2][3] * m.m[3][1] -
	     m.m[0][3] * m.m[1][0] * m.m[2][1] * m.m[3][2] + m.m[0][3] * m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[1][0] * m.m[2][3] * m.m[3][2] +
	     m.m[0][1] * m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[2][0] * m.m[3][2] - m.m[0][3] * m.m[1][2] * m.m[2][0] * m.m[3][1] -
	     m.m[0][2] * m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] * m.m[3][2] - m.m[0][1] * m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[0][2] * m.m[1][3] * m.m[2][1] * m.m[3][0] -
	     m.m[0][3] * m.m[1][1] * m.m[2][2] * m.m[3][0] + m.m[0][3] * m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[0][2] * m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[0][1] * m.m[1][3] * m.m[2][2] * m.m[3][0]);

	result = result / det;

	return result;
}

Matrix4x4 Transpose(const Matrix4x4& m) {
	Matrix4x4 result;
	result = {
	    {
         {
	            m.m[0][0],
	            m.m[1][0],
	            m.m[2][0],
	            m.m[3][0],
	        }, {
	            m.m[0][1],
	            m.m[1][1],
	            m.m[2][1],
	            m.m[3][1],
	        }, {
	            m.m[0][2],
	            m.m[1][2],
	            m.m[2][2],
	            m.m[3][2],
	        }, {
	            m.m[0][3],
	            m.m[1][3],
	            m.m[2][3],
	            m.m[3][3],
	        }, }
    };
	return result;
}

Matrix4x4 MakeIdentity() {
	return {
	    {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
    };
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	return {
	    {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {translate.x, translate.y, translate.z, 1.0f}}
    };
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	return {
	    {{scale.x, 0.0f, 0.0f, 0.0f}, {0.0f, scale.y, 0.0f, 0.0f}, {0.0f, 0.0f, scale.z, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
    };
}

Matrix4x4 MakeRotateXMatrix(float radian) {
	return {
	    {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, std::cos(radian), std::sin(radian), 0.0f}, {0.0f, -std::sin(radian), std::cos(radian), 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
    };
}

Matrix4x4 MakeRotateYMatrix(float radian) {
	return {
	    {{std::cos(radian), 0.0f, -std::sin(radian), 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {std::sin(radian), 0.0f, std::cos(radian), 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
    };
}

Matrix4x4 MakeRotateZMatrix(float radian) {
	return {
	    {{std::cos(radian), std::sin(radian), 0.0f, 0.0f}, {-std::sin(radian), std::cos(radian), 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
    };
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 s = MakeScaleMatrix(scale);
	Matrix4x4 r = MakeRotateXMatrix(rotate.x) * MakeRotateYMatrix(rotate.y) * MakeRotateZMatrix(rotate.z);
	Matrix4x4 t = MakeTranslateMatrix(translate);
	Matrix4x4 w = s * r * t;
	return w;
}

Matrix4x4 MakeRotateMatrix(const Vector3& rotate) {
	return MakeRotateXMatrix(rotate.x) * MakeRotateYMatrix(rotate.y) * MakeRotateZMatrix(rotate.z);
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	return {
	    {{(1.0f / std::tan(fovY / 2.0f)) / aspectRatio, 0.0f, 0.0f, 0.0f},
	     {0.0f, (1.0f / std::tan(fovY / 2.0f)), 0.0f, 0.0f},
	     {0.0f, 0.0f, farClip / (farClip - nearClip), 1.0f},
	     {0.0f, 0.0f, -nearClip * farClip / (farClip - nearClip), 0.0f}}
    };
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	return {
	    {{2.0f / (right - left), 0.0f, 0.0f, 0.0f},
	     {0.0f, 2.0f / (top - bottom), 0.0f, 0.0f},
	     {0.0f, 0.0f, 1.0f / (farClip - nearClip), 0.0f},
	     {(left + right) / (left - right), (top + bottom) / (bottom - top), nearClip / (nearClip - farClip), 1.0f}}
    };
}

Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	if (minDepth > maxDepth) {
		float tmp = minDepth;
		minDepth = maxDepth;
		maxDepth = tmp;
	}

	return {
	    {{width / 2.0f, 0.0f, 0.0f, 0.0f}, {0.0f, -(height / 2.0f), 0.0f, 0.0f}, {0.0f, 0.0f, maxDepth - minDepth, 0.0f}, {left + (width / 2.0f), top + (height / 2.0f), minDepth, 1.0f}}
    };
}
TransformationMatrix MakeBillboardMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate, const Camera& camera) { // ワールド行列の生成
	Matrix4x4 billboardMatrix = kBackToFrontMatrix * Inverse(camera.matView);
	billboardMatrix.m[3][0] = 0.0f; // 平行移動成分は要らない
	billboardMatrix.m[3][1] = 0.0f;
	billboardMatrix.m[3][2] = 0.0f;

	// 回転対応
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);

	Matrix4x4 rotateMatrix = rotateZMatrix * billboardMatrix;
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 worldMatrix = scaleMatrix * rotateMatrix * translateMatrix;

	TransformationMatrix result;
	result.World = worldMatrix;
	result.WVP = worldMatrix * camera.matView * camera.matProjection;
	result.WorldInverseTranspose = Transpose(Inverse(worldMatrix));

	return result;
}
} // namespace KujataEngine
