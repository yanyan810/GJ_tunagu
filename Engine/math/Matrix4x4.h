// Matrix4x4.h
#pragma once
#include <cmath>
#include "Vector3.h"
#include <random>
#include <assimp/matrix4x4.h> 

class Matrix4x4 {
public:
	float m[4][4];

	Matrix4x4();

	static Matrix4x4 MakeIdentity4x4();
	static Matrix4x4 Translation(const Vector3& translation);
	static Matrix4x4 Scale(const Vector3& scale);
	static Matrix4x4 RotateX(float angleRad);
	static Matrix4x4 RotateY(float angleRad);
	static Matrix4x4 RotateZ(float angleRad);
	static Matrix4x4 RotateXYZ(float angleX, float angleY, float angleZ);
	static Matrix4x4 PerspectiveFov(float fovY, float aspect, float nearZ, float farZ);
	static Matrix4x4 MakeScaleMatrix(const Matrix4x4& m);
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);

	static Matrix4x4 FromAiMatrix(const aiMatrix4x4& a)
	{
		Matrix4x4 m{};

		// Assimp の aiMatrix4x4 をそのまま自前の行列にコピー（転置しない）
		m.m[0][0] = a.a1;  m.m[0][1] = a.a2;  m.m[0][2] = a.a3;  m.m[0][3] = a.a4;
		m.m[1][0] = a.b1;  m.m[1][1] = a.b2;  m.m[1][2] = a.b3;  m.m[1][3] = a.b4;
		m.m[2][0] = a.c1;  m.m[2][1] = a.c2;  m.m[2][2] = a.c3;  m.m[2][3] = a.c4;  
		m.m[3][0] = a.d1;  m.m[3][1] = a.d2;  m.m[3][2] = a.d3;  m.m[3][3] = a.d4;

		return m;
	}

	static Matrix4x4 Transpose(const Matrix4x4& a);

	static Matrix4x4 MakeRotateZMatrix(float angleRad);

	static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translation);
	static Matrix4x4 Inverse(const Matrix4x4& m);

	static Matrix4x4 MakePerspectivFovMatrix(float fovY, float aspect, float nearZ, float farZ);
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	static Matrix4x4 MakeViewMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);

	//クロス積
	static Vector3 Cross(const Vector3& v1, const Vector3& v2);


	//正規化
	static Vector3 Normalize(const Vector3& vector);

	static Vector3 Subtract(const Vector3& v1, const Vector3& v2);
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearZ, float farZ);

	// ★ 追加：行列の掛け算オペレータ
	Matrix4x4 operator*(const Matrix4x4& rhs) const;
	Matrix4x4& operator*=(const Matrix4x4& rhs);

	// クォータニオンの構造体がない場合、簡易版
	struct Quat { float x, y, z, w; };

	static Quat Normalize(const Quat& q)
	{
		float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
		return { q.x / len, q.y / len, q.z / len, q.w / len };
	}

	static Quat Slerp(const Quat& a, const Quat& b, float t)
	{
		// 内積
		float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

		// 逆向きの場合は片方を反転
		Quat bb = b;
		if (dot < 0.0f) {
			dot = -dot;
			bb = { -b.x, -b.y, -b.z, -b.w };
		}

		if (dot > 0.9995f) {
			// ほぼ同じ → Lerp でOK
			Quat r{
				a.x + (bb.x - a.x) * t,
				a.y + (bb.y - a.y) * t,
				a.z + (bb.z - a.z) * t,
				a.w + (bb.w - a.w) * t
			};
			return Normalize(r);
		}

		// θ を求める
		float theta = acosf(dot);
		float sinT = sinf(theta);

		float w1 = sinf((1.0f - t) * theta) / sinT;
		float w2 = sinf(t * theta) / sinT;

		return {
			a.x * w1 + bb.x * w2,
			a.y * w1 + bb.y * w2,
			a.z * w1 + bb.z * w2,
			a.w * w1 + bb.w * w2
		};
	}


};