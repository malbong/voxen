#pragma once

#include "Block.h"
#include "Instance.h"
#include <tuple>

using PosInt3 = std::tuple<int, int, int>;

struct PosInt3Hash {
	std::size_t operator()(const PosInt3& t) const
	{
		std::size_t h1 = std::hash<int>{}(std::get<0>(t));
		std::size_t h2 = std::hash<int>{}(std::get<1>(t));
		std::size_t h3 = std::hash<int>{}(std::get<2>(t));

		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

struct PosInt3Equal {
	bool operator()(const PosInt3& a, const PosInt3& b) const { return a == b; }
};

template <typename T> using PosHashMap = std::unordered_map<PosInt3, T, PosInt3Hash, PosInt3Equal>;

using PosHashSet = std::unordered_set<PosInt3, PosInt3Hash, PosInt3Equal>;

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
