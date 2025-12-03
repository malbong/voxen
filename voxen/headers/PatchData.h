#pragma once

#include "Block.h"
#include "Instance.h"

struct PatchData {
	int localX;
	int localY;
	int localZ;
	Block block;
	Instance instance;
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