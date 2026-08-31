#pragma once
#include <vector>
#include <cstdint>
#include "Model.h"

namespace GeometryGenerator {

	std::vector<Model::VertexData>
		GenerateRingTriListXY(uint32_t divide, float outerR, float innerR);

	std::vector<Model::VertexData>
		GeneratePlaneTriListXY(float width, float height);

	std::vector<Model::VertexData>
		GenerateTriangleTriListXY(float width, float height);

	std::vector<Model::VertexData>
		GenerateStarTriListXY(float outerRadius, float innerRadius, uint32_t points = 5);

	std::vector<Model::VertexData>
		GenerateDiamondTriListXY(float width, float height);

	std::vector<Model::VertexData>
		GenerateBoxTriList(float width, float height, float depth);

	std::vector<Model::VertexData>
		GenerateSphereTriList(uint32_t sliceCount, uint32_t stackCount, float radius);

	std::vector<Model::VertexData>
		GenerateTorusTriList(uint32_t majorDivide, uint32_t minorDivide, float majorRadius, float minorRadius);

	std::vector<Model::VertexData>
		GenerateCylinderTriList(uint32_t divide, float radius, float height);

	std::vector<Model::VertexData>
		GenerateConeTriList(uint32_t divide, float radius, float height);

	std::vector<Model::VertexData>
		GenerateCapsuleTriList(uint32_t sliceCount, uint32_t hemisphereStackCount, float radius, float cylinderHeight);

}
