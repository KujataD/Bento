#include "Matrix4x4.h"
#include "../3d/Camera.h"
#include "../3d/WorldTransform.h"

namespace KujataEngine {
Matrix4x4 Matrix4x4::operator+(const Matrix4x4& other) const {
	return {
	    {
         {
	            this->m[0][0] + other.m[0][0],
	            this->m[0][1] + other.m[0][1],
	            this->m[0][2] + other.m[0][2],
	            this->m[0][3] + other.m[0][3],
	        }, {
	            this->m[1][0] + other.m[1][0],
	            this->m[1][1] + other.m[1][1],
	            this->m[1][2] + other.m[1][2],
	            this->m[1][3] + other.m[1][3],
	        }, {
	            this->m[2][0] + other.m[2][0],
	            this->m[2][1] + other.m[2][1],
	            this->m[2][2] + other.m[2][2],
	            this->m[2][3] + other.m[2][3],
	        }, {
	            this->m[3][0] + other.m[3][0],
	            this->m[3][1] + other.m[3][1],
	            this->m[3][2] + other.m[3][2],
	            this->m[3][3] + other.m[3][3],
	        }, }
    };
}

Matrix4x4 Matrix4x4::operator-(const Matrix4x4& other) const {
	return {
	    {
         {
	            this->m[0][0] - other.m[0][0],
	            this->m[0][1] - other.m[0][1],
	            this->m[0][2] - other.m[0][2],
	            this->m[0][3] - other.m[0][3],
	        }, {
	            this->m[1][0] - other.m[1][0],
	            this->m[1][1] - other.m[1][1],
	            this->m[1][2] - other.m[1][2],
	            this->m[1][3] - other.m[1][3],
	        }, {
	            this->m[2][0] - other.m[2][0],
	            this->m[2][1] - other.m[2][1],
	            this->m[2][2] - other.m[2][2],
	            this->m[2][3] - other.m[2][3],
	        }, {
	            this->m[3][0] - other.m[3][0],
	            this->m[3][1] - other.m[3][1],
	            this->m[3][2] - other.m[3][2],
	            this->m[3][3] - other.m[3][3],
	        }, }
    };
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const {
	return {
	    {
         {
	            this->m[0][0] * other.m[0][0] + this->m[0][1] * other.m[1][0] + this->m[0][2] * other.m[2][0] + this->m[0][3] * other.m[3][0],
	            this->m[0][0] * other.m[0][1] + this->m[0][1] * other.m[1][1] + this->m[0][2] * other.m[2][1] + this->m[0][3] * other.m[3][1],
	            this->m[0][0] * other.m[0][2] + this->m[0][1] * other.m[1][2] + this->m[0][2] * other.m[2][2] + this->m[0][3] * other.m[3][2],
	            this->m[0][0] * other.m[0][3] + this->m[0][1] * other.m[1][3] + this->m[0][2] * other.m[2][3] + this->m[0][3] * other.m[3][3],
	        }, {this->m[1][0] * other.m[0][0] + this->m[1][1] * other.m[1][0] + this->m[1][2] * other.m[2][0] + this->m[1][3] * other.m[3][0],
	         this->m[1][0] * other.m[0][1] + this->m[1][1] * other.m[1][1] + this->m[1][2] * other.m[2][1] + this->m[1][3] * other.m[3][1],
	         this->m[1][0] * other.m[0][2] + this->m[1][1] * other.m[1][2] + this->m[1][2] * other.m[2][2] + this->m[1][3] * other.m[3][2],
	         this->m[1][0] * other.m[0][3] + this->m[1][1] * other.m[1][3] + this->m[1][2] * other.m[2][3] + this->m[1][3] * other.m[3][3]},
         {
	            this->m[2][0] * other.m[0][0] + this->m[2][1] * other.m[1][0] + this->m[2][2] * other.m[2][0] + this->m[2][3] * other.m[3][0],
	            this->m[2][0] * other.m[0][1] + this->m[2][1] * other.m[1][1] + this->m[2][2] * other.m[2][1] + this->m[2][3] * other.m[3][1],
	            this->m[2][0] * other.m[0][2] + this->m[2][1] * other.m[1][2] + this->m[2][2] * other.m[2][2] + this->m[2][3] * other.m[3][2],
	            this->m[2][0] * other.m[0][3] + this->m[2][1] * other.m[1][3] + this->m[2][2] * other.m[2][3] + this->m[2][3] * other.m[3][3],
	        }, {
	            this->m[3][0] * other.m[0][0] + this->m[3][1] * other.m[1][0] + this->m[3][2] * other.m[2][0] + this->m[3][3] * other.m[3][0],
	            this->m[3][0] * other.m[0][1] + this->m[3][1] * other.m[1][1] + this->m[3][2] * other.m[2][1] + this->m[3][3] * other.m[3][1],
	            this->m[3][0] * other.m[0][2] + this->m[3][1] * other.m[1][2] + this->m[3][2] * other.m[2][2] + this->m[3][3] * other.m[3][2],
	            this->m[3][0] * other.m[0][3] + this->m[3][1] * other.m[1][3] + this->m[3][2] * other.m[2][3] + this->m[3][3] * other.m[3][3],
	        }, }
    };
}

Matrix4x4 Matrix4x4::operator/(float scalar) const {
	return {
	    {{this->m[0][0] / scalar, this->m[0][1] / scalar, this->m[0][2] / scalar, this->m[0][3] / scalar},
	     {this->m[1][0] / scalar, this->m[1][1] / scalar, this->m[1][2] / scalar, this->m[1][3] / scalar},
	     {this->m[2][0] / scalar, this->m[2][1] / scalar, this->m[2][2] / scalar, this->m[2][3] / scalar},
	     {this->m[3][0] / scalar, this->m[3][1] / scalar, this->m[3][2] / scalar, this->m[3][3] / scalar}}
    };
}

} // namespace KujataEngine