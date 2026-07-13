#pragma once

#include "Block.h"
#include "Instance.h"

struct PatchData {
	int localX;
	int localY;
	int localZ;
	Block block;
	Instance instance;

	PatchData() = default;
	PatchData(
		int x, int y, int z, Block aBlock, const Instance& aInstance, int baseSize, bool needWrap)
		: block(aBlock), instance(aInstance)
	{
		localX = needWrap ? Utils::WrapToBase(x, baseSize) : x;
		localY = needWrap ? Utils::WrapToBase(y, baseSize) : y;
		localZ = needWrap ? Utils::WrapToBase(z, baseSize) : z;
	}
	PatchData(
		Vector3 position, Block aBlock, const Instance& aInstance, int baseSize, bool needWrap)
		: PatchData((int)position.x, (int)position.y, (int)position.z, aBlock, aInstance, baseSize,
			  needWrap)
	{
	}

	static void GenerateEdgePatchEntry(int x, int y, int z, Vector3 chunkPosition, BLOCK_TYPE blockType,
		int baseSize, std::pair<PosInt3, PatchData>* outEdgePatchEntry, int& outEdgePatchEntryCount)
	{
		outEdgePatchEntryCount = 0;

		int xEdgeDir = (x == 0) ? -1 : ((x == baseSize - 1) ? 1 : 0);
		if (xEdgeDir) {
			Vector3 patchChunkOffsetPos = chunkPosition;
			patchChunkOffsetPos.x += xEdgeDir * baseSize;

			PosInt3 patchChunkOffsetPosInt3 = Utils::VectorToPosInt3(patchChunkOffsetPos);

			int newX = xEdgeDir == -1 ? baseSize : -1;

			PatchData patchData(newX, y, z, Block(blockType), Instance(), baseSize, false);

			outEdgePatchEntry[outEdgePatchEntryCount++] =
				std::make_pair(patchChunkOffsetPosInt3, patchData);
		}

		int yEdgeDir = (y == 0) ? -1 : ((y == baseSize - 1) ? 1 : 0);
		if (yEdgeDir) {
			Vector3 patchChunkOffsetPos = chunkPosition;
			patchChunkOffsetPos.y += yEdgeDir * baseSize;

			PosInt3 patchChunkOffsetPosInt3 = Utils::VectorToPosInt3(patchChunkOffsetPos);

			int newY = yEdgeDir == -1 ? baseSize : -1;

			PatchData patchData(x, newY, z, blockType, Instance(), baseSize, false);

			outEdgePatchEntry[outEdgePatchEntryCount++] =
				std::make_pair(patchChunkOffsetPosInt3, patchData);
		}

		int zEdgeDir = (z == 0) ? -1 : ((z == baseSize - 1) ? 1 : 0);
		if (zEdgeDir) {
			Vector3 patchChunkOffsetPos = chunkPosition;
			patchChunkOffsetPos.z += zEdgeDir * baseSize;

			PosInt3 patchChunkOffsetPosInt3 = Utils::VectorToPosInt3(patchChunkOffsetPos);

			int newZ = zEdgeDir == -1 ? baseSize : -1;

			PatchData patchData(x, y, newZ, blockType, Instance(), baseSize, false);

			outEdgePatchEntry[outEdgePatchEntryCount++] =
				std::make_pair(patchChunkOffsetPosInt3, patchData);
		}
	}

	static void GenerateEdgePatchEntry(Vector3 position, Vector3 chunkPosition, BLOCK_TYPE blockType,
		int baseSize, std::pair<PosInt3, PatchData>* outEdgePatchEntry, int& outEdgePatchEntryCount)
	{
		return GenerateEdgePatchEntry((int)position.x, (int)position.y, (int)position.z,
			chunkPosition, blockType, baseSize, outEdgePatchEntry, outEdgePatchEntryCount);
	}
};

struct PatchDataHash {
	std::size_t operator()(const PatchData& t) const
	{
		std::size_t h1 = std::hash<int>{}(t.localX);
		std::size_t h2 = std::hash<int>{}(t.localY);
		std::size_t h3 = std::hash<int>{}(t.localZ);

		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

struct PatchDataEqual {
	bool operator()(const PatchData& a, const PatchData& b) const
	{
		return (a.localX == b.localX && a.localY == b.localY && a.localZ == b.localZ);
	}
};

using PatchDataHashSet = std::unordered_set<PatchData, PatchDataHash, PatchDataEqual>;