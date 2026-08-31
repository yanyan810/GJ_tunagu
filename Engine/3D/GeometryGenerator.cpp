#include "GeometryGenerator.h"
#include <numbers>
#include <cmath>
#include <algorithm>

namespace {

	Model::VertexData MakeVertex(
		float px, float py, float pz,
		float u, float v,
		float nx, float ny, float nz)
	{
		Model::VertexData out{};
		out.position = { px, py, pz, 1.0f };
		out.texcoord = { u, v };
		out.normal = { nx, ny, nz };
		return out;
	}

	void PushQuad(
		std::vector<Model::VertexData>& v,
		const Model::VertexData& a,
		const Model::VertexData& b,
		const Model::VertexData& c,
		const Model::VertexData& d)
	{
		// a-b-c, a-c-d
		v.push_back(a);
		v.push_back(b);
		v.push_back(c);

		v.push_back(a);
		v.push_back(c);
		v.push_back(d);
	}

} // namespace

std::vector<Model::VertexData>
GeometryGenerator::GenerateRingTriListXY(uint32_t divide, float outerR, float innerR) {
	std::vector<Model::VertexData> v;
	v.reserve(divide * 6);

	const float pi = std::numbers::pi_v<float>;
	const float step = (2.0f * pi) / float(divide);

	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = step * float(i);
		float a1 = step * float(i + 1);

		float c0 = std::cosf(a0), s0 = std::sinf(a0);
		float c1 = std::cosf(a1), s1 = std::sinf(a1);

		float ox0 = c0 * outerR, oy0 = s0 * outerR;
		float ix0 = c0 * innerR, iy0 = s0 * innerR;
		float ox1 = c1 * outerR, oy1 = s1 * outerR;
		float ix1 = c1 * innerR, iy1 = s1 * innerR;

		float u0 = float(i) / float(divide);
		float u1 = float(i + 1) / float(divide);

		v.push_back(MakeVertex(ox0, oy0, 0.0f, u0, 0.0f, 0.0f, 0.0f, 1.0f)); // ①
		v.push_back(MakeVertex(ox1, oy1, 0.0f, u1, 0.0f, 0.0f, 0.0f, 1.0f)); // ②
		v.push_back(MakeVertex(ix0, iy0, 0.0f, u0, 1.0f, 0.0f, 0.0f, 1.0f)); // ③

		v.push_back(MakeVertex(ox1, oy1, 0.0f, u1, 0.0f, 0.0f, 0.0f, 1.0f)); // ②
		v.push_back(MakeVertex(ix1, iy1, 0.0f, u1, 1.0f, 0.0f, 0.0f, 1.0f)); // ④
		v.push_back(MakeVertex(ix0, iy0, 0.0f, u0, 1.0f, 0.0f, 0.0f, 1.0f)); // ③
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GeneratePlaneTriListXY(float width, float height) {
	std::vector<Model::VertexData> v;
	v.reserve(6);

	float hw = width * 0.5f;
	float hh = height * 0.5f;

	Model::VertexData a = MakeVertex(-hw, -hh, 0.0f, 0.0f, 1.0f, 0, 0, 1);
	Model::VertexData b = MakeVertex(-hw, hh, 0.0f, 0.0f, 0.0f, 0, 0, 1);
	Model::VertexData c = MakeVertex(hw, hh, 0.0f, 1.0f, 0.0f, 0, 0, 1);
	Model::VertexData d = MakeVertex(hw, -hh, 0.0f, 1.0f, 1.0f, 0, 0, 1);

	PushQuad(v, a, b, c, d);
	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateTriangleTriListXY(float width, float height) {
	std::vector<Model::VertexData> v;
	v.reserve(3);

	float hw = width * 0.5f;
	float hh = height * 0.5f;

	v.push_back(MakeVertex(0.0f, hh, 0.0f, 0.5f, 0.0f, 0, 0, 1));
	v.push_back(MakeVertex(-hw, -hh, 0.0f, 0.0f, 1.0f, 0, 0, 1));
	v.push_back(MakeVertex(hw, -hh, 0.0f, 1.0f, 1.0f, 0, 0, 1));

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateStarTriListXY(float outerRadius, float innerRadius, uint32_t points) {
	std::vector<Model::VertexData> v;
	points = std::max<uint32_t>(2, points);
	v.reserve(points * 2 * 3);

	const float pi = std::numbers::pi_v<float>;
	const uint32_t vertexCount = points * 2;
	const float step = (2.0f * pi) / float(vertexCount);
	const float start = pi * 0.5f;

	Model::VertexData center = MakeVertex(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0, 0, 1);
	for (uint32_t i = 0; i < vertexCount; ++i) {
		float a0 = start + step * float(i);
		float a1 = start + step * float(i + 1);
		float r0 = (i % 2 == 0) ? outerRadius : innerRadius;
		float r1 = ((i + 1) % 2 == 0) ? outerRadius : innerRadius;

		Model::VertexData p0 = MakeVertex(
			std::cosf(a0) * r0,
			std::sinf(a0) * r0,
			0.0f,
			0.5f + std::cosf(a0) * 0.5f,
			0.5f - std::sinf(a0) * 0.5f,
			0, 0, 1);
		Model::VertexData p1 = MakeVertex(
			std::cosf(a1) * r1,
			std::sinf(a1) * r1,
			0.0f,
			0.5f + std::cosf(a1) * 0.5f,
			0.5f - std::sinf(a1) * 0.5f,
			0, 0, 1);

		v.push_back(center);
		v.push_back(p1);
		v.push_back(p0);
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateDiamondTriListXY(float width, float height) {
	std::vector<Model::VertexData> v;
	v.reserve(6);

	float hw = width * 0.5f;
	float hh = height * 0.5f;

	Model::VertexData top = MakeVertex(0.0f, hh, 0.0f, 0.5f, 0.0f, 0, 0, 1);
	Model::VertexData left = MakeVertex(-hw, 0.0f, 0.0f, 0.0f, 0.5f, 0, 0, 1);
	Model::VertexData right = MakeVertex(hw, 0.0f, 0.0f, 1.0f, 0.5f, 0, 0, 1);
	Model::VertexData bottom = MakeVertex(0.0f, -hh, 0.0f, 0.5f, 1.0f, 0, 0, 1);

	v.push_back(top);
	v.push_back(left);
	v.push_back(right);

	v.push_back(right);
	v.push_back(left);
	v.push_back(bottom);

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateBoxTriList(float width, float height, float depth) {
	std::vector<Model::VertexData> v;
	v.reserve(36);

	float hw = width * 0.5f;
	float hh = height * 0.5f;
	float hd = depth * 0.5f;

	// front (+Z)
	PushQuad(v,
		MakeVertex(-hw, -hh, hd, 0, 1, 0, 0, 1),
		MakeVertex(-hw, hh, hd, 0, 0, 0, 0, 1),
		MakeVertex(hw, hh, hd, 1, 0, 0, 0, 1),
		MakeVertex(hw, -hh, hd, 1, 1, 0, 0, 1));

	// back (-Z)
	PushQuad(v,
		MakeVertex(hw, -hh, -hd, 0, 1, 0, 0, -1),
		MakeVertex(hw, hh, -hd, 0, 0, 0, 0, -1),
		MakeVertex(-hw, hh, -hd, 1, 0, 0, 0, -1),
		MakeVertex(-hw, -hh, -hd, 1, 1, 0, 0, -1));

	// left (-X)
	PushQuad(v,
		MakeVertex(-hw, -hh, -hd, 0, 1, -1, 0, 0),
		MakeVertex(-hw, hh, -hd, 0, 0, -1, 0, 0),
		MakeVertex(-hw, hh, hd, 1, 0, -1, 0, 0),
		MakeVertex(-hw, -hh, hd, 1, 1, -1, 0, 0));

	// right (+X)
	PushQuad(v,
		MakeVertex(hw, -hh, hd, 0, 1, 1, 0, 0),
		MakeVertex(hw, hh, hd, 0, 0, 1, 0, 0),
		MakeVertex(hw, hh, -hd, 1, 0, 1, 0, 0),
		MakeVertex(hw, -hh, -hd, 1, 1, 1, 0, 0));

	// top (+Y)
	PushQuad(v,
		MakeVertex(-hw, hh, hd, 0, 1, 0, 1, 0),
		MakeVertex(-hw, hh, -hd, 0, 0, 0, 1, 0),
		MakeVertex(hw, hh, -hd, 1, 0, 0, 1, 0),
		MakeVertex(hw, hh, hd, 1, 1, 0, 1, 0));

	// bottom (-Y)
	PushQuad(v,
		MakeVertex(-hw, -hh, -hd, 0, 1, 0, -1, 0),
		MakeVertex(-hw, -hh, hd, 0, 0, 0, -1, 0),
		MakeVertex(hw, -hh, hd, 1, 0, 0, -1, 0),
		MakeVertex(hw, -hh, -hd, 1, 1, 0, -1, 0));

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateSphereTriList(uint32_t sliceCount, uint32_t stackCount, float radius) {
	std::vector<Model::VertexData> v;
	const float pi = std::numbers::pi_v<float>;

	for (uint32_t stack = 0; stack < stackCount; ++stack) {
		float phi0 = pi * float(stack) / float(stackCount);
		float phi1 = pi * float(stack + 1) / float(stackCount);

		for (uint32_t slice = 0; slice < sliceCount; ++slice) {
			float theta0 = 2.0f * pi * float(slice) / float(sliceCount);
			float theta1 = 2.0f * pi * float(slice + 1) / float(sliceCount);

			auto sphereV = [&](float phi, float theta, float u, float vv) {
				float x = radius * std::sinf(phi) * std::cosf(theta);
				float y = radius * std::cosf(phi);
				float z = radius * std::sinf(phi) * std::sinf(theta);

				float len = std::sqrt(x * x + y * y + z * z);
				float nx = (len > 0.0f) ? x / len : 0.0f;
				float ny = (len > 0.0f) ? y / len : 1.0f;
				float nz = (len > 0.0f) ? z / len : 0.0f;

				return MakeVertex(x, y, z, u, vv, nx, ny, nz);
				};

			Model::VertexData a = sphereV(phi0, theta0, float(slice) / sliceCount, float(stack) / stackCount);
			Model::VertexData b = sphereV(phi1, theta0, float(slice) / sliceCount, float(stack + 1) / stackCount);
			Model::VertexData c = sphereV(phi1, theta1, float(slice + 1) / sliceCount, float(stack + 1) / stackCount);
			Model::VertexData d = sphereV(phi0, theta1, float(slice + 1) / sliceCount, float(stack) / stackCount);

			PushQuad(v, a, b, c, d);
		}
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateTorusTriList(uint32_t majorDivide, uint32_t minorDivide, float majorRadius, float minorRadius) {
	std::vector<Model::VertexData> v;
	const float pi = std::numbers::pi_v<float>;

	for (uint32_t i = 0; i < majorDivide; ++i) {
		float u0 = 2.0f * pi * float(i) / float(majorDivide);
		float u1 = 2.0f * pi * float(i + 1) / float(majorDivide);

		for (uint32_t j = 0; j < minorDivide; ++j) {
			float v0 = 2.0f * pi * float(j) / float(minorDivide);
			float v1 = 2.0f * pi * float(j + 1) / float(minorDivide);

			auto torusV = [&](float u, float vv, float tu, float tv) {
				float cu = std::cosf(u), su = std::sinf(u);
				float cv = std::cosf(vv), sv = std::sinf(vv);

				float x = (majorRadius + minorRadius * cv) * cu;
				float y = minorRadius * sv;
				float z = (majorRadius + minorRadius * cv) * su;

				float nx = cv * cu;
				float ny = sv;
				float nz = cv * su;

				return MakeVertex(x, y, z, tu, tv, nx, ny, nz);
				};

			Model::VertexData a = torusV(u0, v0, float(i) / majorDivide, float(j) / minorDivide);
			Model::VertexData b = torusV(u1, v0, float(i + 1) / majorDivide, float(j) / minorDivide);
			Model::VertexData c = torusV(u1, v1, float(i + 1) / majorDivide, float(j + 1) / minorDivide);
			Model::VertexData d = torusV(u0, v1, float(i) / majorDivide, float(j + 1) / minorDivide);

			PushQuad(v, a, b, c, d);
		}
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateCylinderTriList(uint32_t divide, float radius, float height) {
	std::vector<Model::VertexData> v;
	const float pi = std::numbers::pi_v<float>;
	float hh = height * 0.5f;

	// side
	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = 2.0f * pi * float(i) / float(divide);
		float a1 = 2.0f * pi * float(i + 1) / float(divide);

		float c0 = std::cosf(a0), s0 = std::sinf(a0);
		float c1 = std::cosf(a1), s1 = std::sinf(a1);

		Model::VertexData a = MakeVertex(radius * c0, -hh, radius * s0, float(i) / divide, 1, c0, 0, s0);
		Model::VertexData b = MakeVertex(radius * c0, hh, radius * s0, float(i) / divide, 0, c0, 0, s0);
		Model::VertexData c = MakeVertex(radius * c1, hh, radius * s1, float(i + 1) / divide, 0, c1, 0, s1);
		Model::VertexData d = MakeVertex(radius * c1, -hh, radius * s1, float(i + 1) / divide, 1, c1, 0, s1);

		PushQuad(v, a, b, c, d);
	}

	// top cap
	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = 2.0f * pi * float(i) / float(divide);
		float a1 = 2.0f * pi * float(i + 1) / float(divide);

		v.push_back(MakeVertex(0, hh, 0, 0.5f, 0.5f, 0, 1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a0), hh, radius * std::sinf(a0), 0, 0, 0, 1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a1), hh, radius * std::sinf(a1), 1, 0, 0, 1, 0));
	}

	// bottom cap
	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = 2.0f * pi * float(i) / float(divide);
		float a1 = 2.0f * pi * float(i + 1) / float(divide);

		v.push_back(MakeVertex(0, -hh, 0, 0.5f, 0.5f, 0, -1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a1), -hh, radius * std::sinf(a1), 1, 0, 0, -1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a0), -hh, radius * std::sinf(a0), 0, 0, 0, -1, 0));
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateConeTriList(uint32_t divide, float radius, float height) {
	std::vector<Model::VertexData> v;
	const float pi = std::numbers::pi_v<float>;
	float hh = height * 0.5f;

	// side
	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = 2.0f * pi * float(i) / float(divide);
		float a1 = 2.0f * pi * float(i + 1) / float(divide);

		float x0 = radius * std::cosf(a0);
		float z0 = radius * std::sinf(a0);
		float x1 = radius * std::cosf(a1);
		float z1 = radius * std::sinf(a1);

		Vector3 e0 = { x0, -hh, z0 };
		Vector3 e1 = { x1, -hh, z1 };
		Vector3 apex = { 0.0f, hh, 0.0f };

		Vector3 edge1 = apex - e0;
		Vector3 edge2 = e1 - e0;
		Vector3 n = Matrix4x4::Normalize(Matrix4x4::Cross(edge1, edge2));

		v.push_back(MakeVertex(0.0f, hh, 0.0f, 0.5f, 0.0f, n.x, n.y, n.z));
		v.push_back(MakeVertex(x0, -hh, z0, 0.0f, 1.0f, n.x, n.y, n.z));
		v.push_back(MakeVertex(x1, -hh, z1, 1.0f, 1.0f, n.x, n.y, n.z));
	}

	// bottom cap
	for (uint32_t i = 0; i < divide; ++i) {
		float a0 = 2.0f * pi * float(i) / float(divide);
		float a1 = 2.0f * pi * float(i + 1) / float(divide);

		v.push_back(MakeVertex(0, -hh, 0, 0.5f, 0.5f, 0, -1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a1), -hh, radius * std::sinf(a1), 1, 0, 0, -1, 0));
		v.push_back(MakeVertex(radius * std::cosf(a0), -hh, radius * std::sinf(a0), 0, 0, 0, -1, 0));
	}

	return v;
}

std::vector<Model::VertexData>
GeometryGenerator::GenerateCapsuleTriList(uint32_t sliceCount, uint32_t hemisphereStackCount, float radius, float cylinderHeight) {
	std::vector<Model::VertexData> v;
	sliceCount = std::max<uint32_t>(3, sliceCount);
	hemisphereStackCount = std::max<uint32_t>(2, hemisphereStackCount);
	cylinderHeight = std::max(0.0f, cylinderHeight);

	const float pi = std::numbers::pi_v<float>;
	const float halfCylinder = cylinderHeight * 0.5f;

	v.reserve(sliceCount * (2 + hemisphereStackCount * 2) * 6);

	// Cylinder body
	if (cylinderHeight > 0.0f) {
		for (uint32_t i = 0; i < sliceCount; ++i) {
			float a0 = 2.0f * pi * float(i) / float(sliceCount);
			float a1 = 2.0f * pi * float(i + 1) / float(sliceCount);
			float c0 = std::cosf(a0), s0 = std::sinf(a0);
			float c1 = std::cosf(a1), s1 = std::sinf(a1);

			Model::VertexData a = MakeVertex(radius * c0, -halfCylinder, radius * s0, float(i) / sliceCount, 1.0f, c0, 0, s0);
			Model::VertexData b = MakeVertex(radius * c0, halfCylinder, radius * s0, float(i) / sliceCount, 0.5f, c0, 0, s0);
			Model::VertexData c = MakeVertex(radius * c1, halfCylinder, radius * s1, float(i + 1) / sliceCount, 0.5f, c1, 0, s1);
			Model::VertexData d = MakeVertex(radius * c1, -halfCylinder, radius * s1, float(i + 1) / sliceCount, 1.0f, c1, 0, s1);
			PushQuad(v, a, b, c, d);
		}
	}

	auto hemiVertex = [&](float theta, float phi, float yOffset, float vCoord) {
		float sinPhi = std::sinf(phi);
		float x = radius * sinPhi * std::cosf(theta);
		float y = yOffset + radius * std::cosf(phi);
		float z = radius * sinPhi * std::sinf(theta);
		float nx = sinPhi * std::cosf(theta);
		float ny = std::cosf(phi);
		float nz = sinPhi * std::sinf(theta);
		return MakeVertex(x, y, z, theta / (2.0f * pi), vCoord, nx, ny, nz);
	};

	// Top hemisphere: phi 0..pi/2
	for (uint32_t stack = 0; stack < hemisphereStackCount; ++stack) {
		float phi0 = (pi * 0.5f) * float(stack) / float(hemisphereStackCount);
		float phi1 = (pi * 0.5f) * float(stack + 1) / float(hemisphereStackCount);
		for (uint32_t slice = 0; slice < sliceCount; ++slice) {
			float theta0 = 2.0f * pi * float(slice) / float(sliceCount);
			float theta1 = 2.0f * pi * float(slice + 1) / float(sliceCount);
			float v0 = 0.5f * float(stack) / float(hemisphereStackCount);
			float v1 = 0.5f * float(stack + 1) / float(hemisphereStackCount);

			Model::VertexData a = hemiVertex(theta0, phi0, halfCylinder, v0);
			Model::VertexData b = hemiVertex(theta0, phi1, halfCylinder, v1);
			Model::VertexData c = hemiVertex(theta1, phi1, halfCylinder, v1);
			Model::VertexData d = hemiVertex(theta1, phi0, halfCylinder, v0);
			PushQuad(v, a, b, c, d);
		}
	}

	// Bottom hemisphere: phi pi/2..pi
	for (uint32_t stack = 0; stack < hemisphereStackCount; ++stack) {
		float phi0 = (pi * 0.5f) + (pi * 0.5f) * float(stack) / float(hemisphereStackCount);
		float phi1 = (pi * 0.5f) + (pi * 0.5f) * float(stack + 1) / float(hemisphereStackCount);
		for (uint32_t slice = 0; slice < sliceCount; ++slice) {
			float theta0 = 2.0f * pi * float(slice) / float(sliceCount);
			float theta1 = 2.0f * pi * float(slice + 1) / float(sliceCount);
			float v0 = 0.5f + 0.5f * float(stack) / float(hemisphereStackCount);
			float v1 = 0.5f + 0.5f * float(stack + 1) / float(hemisphereStackCount);

			Model::VertexData a = hemiVertex(theta0, phi0, -halfCylinder, v0);
			Model::VertexData b = hemiVertex(theta0, phi1, -halfCylinder, v1);
			Model::VertexData c = hemiVertex(theta1, phi1, -halfCylinder, v1);
			Model::VertexData d = hemiVertex(theta1, phi0, -halfCylinder, v0);
			PushQuad(v, a, b, c, d);
		}
	}

	return v;
}
