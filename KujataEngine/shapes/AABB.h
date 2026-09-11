#pragma once
#include <math/MathUtil.h>

namespace KujataEngine {

struct AABB {
	Vector3 min;
	Vector3 max;

	void SwapMinMax() {
		if (min.x > max.x) {
			std::swap(min.x, max.x);
		}
		if (min.y > max.y) {
			std::swap(min.y, max.y);
		}
		if (min.z > max.z) {
			std::swap(min.z, max.z);
		}
	}
};

struct OBB {
	Vector3 center;
	Vector3 orientations[3];
	Vector3 size;

	Matrix4x4 GetWorldMatrix() const { return MakeAffineMatrixOrientations(orientations, center); }
	void UpdateOBBOrientations(const Vector3& rotate) {
		// 回転行列を生成
		Matrix4x4 rotateMatrix = MakeRotateZMatrix(rotate.z) * MakeRotateYMatrix(rotate.y) * MakeRotateXMatrix(rotate.x);

		// 回転行列から軸を抽出
		orientations[0] = {rotateMatrix.m[0][0], rotateMatrix.m[0][1], rotateMatrix.m[0][2]}; // 0行目
		orientations[1] = {rotateMatrix.m[1][0], rotateMatrix.m[1][1], rotateMatrix.m[1][2]}; // 1行目
		orientations[2] = {rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2]}; // 2行目

		orientations[0] = Normalize(orientations[0]);
		orientations[1] = Normalize(orientations[1]);
		orientations[2] = Normalize(orientations[2]);
	}
};

} // namespace KujataEngine