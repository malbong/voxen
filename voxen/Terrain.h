#pragma once

#include "Utils.h"

#include <directxtk/SimpleMath.h>

#include <queue>
#include <mutex>

using namespace DirectX::SimpleMath;

namespace Terrain {

	static const int MAX_HEIGHT_LEVEL = 256;
	static const int MIN_HEIGHT_LEVEL = 0;
	static const int WATER_HEIGHT_LEVEL = 63;

	static uint32_t HashInt(uint32_t seed, uint32_t solt)
	{
		uint32_t hash = (uint32_t)seed * 73856093 ^ (uint32_t)(solt) * 19349663;

		hash ^= (hash >> 13);
		hash *= 1597334673U;
		hash ^= (hash >> 17);

		return hash;
	}

	static Vector2 Hash(uint32_t x, uint32_t y)
	{
		// https://www.shadertoy.com/view/3dVXDc
		uint32_t u0 = 1597334673U;
		uint32_t u1 = 3812015801U;
		float uf = (1.0f / float(0xffffffffU));

		uint32_t qi = x * u0;
		uint32_t qj = y * u1;

		uint32_t qx = (qi ^ qj) * u0;
		uint32_t qy = (qi ^ qj) * u1;

		float rx = -1.0f + 2.0f * qx * uf;
		float ry = -1.0f + 2.0f * qy * uf;

		return Vector2(rx, ry);
	}

	static Vector3 Hash(uint32_t x, uint32_t y, uint32_t z)
	{
		// https://www.shadertoy.com/view/3dVXDc
		uint32_t u0 = 1597334673U;
		uint32_t u1 = 3812015801U;
		uint32_t u2 = 2798796415U;
		float uf = (1.0f / float(0xffffffffU));

		uint32_t qi = x * u0;
		uint32_t qj = y * u1;
		uint32_t qk = z * u2;

		uint32_t qx = (qi ^ qj ^ qk) * u0;
		uint32_t qy = (qi ^ qj ^ qk) * u1;
		uint32_t qz = (qi ^ qj ^ qk) * u2;

		float rx = -1.0f + 2.0f * qx * uf;
		float ry = -1.0f + 2.0f * qy * uf;
		float rz = -1.0f + 2.0f * qz * uf;

		return Vector3(rx, ry, rz);
	}

	static float GetPerlinNoiseFbm(float x, float y)
	{
		Vector2 p = Vector2(x, y);
		int x0 = (int)floor(x);
		int x1 = x0 + 1;
		int y0 = (int)floor(y);
		int y1 = y0 + 1;

		float n0 = Hash(x0, y0).Dot(p - Vector2((float)x0, (float)y0));
		float n1 = Hash(x1, y0).Dot(p - Vector2((float)x1, (float)y0));
		float n2 = Hash(x0, y1).Dot(p - Vector2((float)x0, (float)y1));
		float n3 = Hash(x1, y1).Dot(p - Vector2((float)x1, (float)y1));

		float i0 = Utils::CubicLerp(n0, n1, p.x - (float)x0);
		float i1 = Utils::CubicLerp(n2, n3, p.x - (float)x0);

		return Utils::CubicLerp(i0, i1, p.y - (float)y0);
	}

	static float GetPerlinNoiseFbm(float x, float y, float z)
	{
		Vector3 p = Vector3(x, y, z);

		int x0 = (int)floor(x);
		int x1 = x0 + 1;
		int y0 = (int)floor(y);
		int y1 = y0 + 1;
		int z0 = (int)floor(z);
		int z1 = z0 + 1;

		float n0 = Hash(x0, y0, z0).Dot(p - Vector3((float)x0, (float)y0, (float)z0));
		float n1 = Hash(x0, y0, z1).Dot(p - Vector3((float)x0, (float)y0, (float)z1));
		float n2 = Hash(x0, y1, z0).Dot(p - Vector3((float)x0, (float)y1, (float)z0));
		float n3 = Hash(x0, y1, z1).Dot(p - Vector3((float)x0, (float)y1, (float)z1));
		float n4 = Hash(x1, y0, z0).Dot(p - Vector3((float)x1, (float)y0, (float)z0));
		float n5 = Hash(x1, y0, z1).Dot(p - Vector3((float)x1, (float)y0, (float)z1));
		float n6 = Hash(x1, y1, z0).Dot(p - Vector3((float)x1, (float)y1, (float)z0));
		float n7 = Hash(x1, y1, z1).Dot(p - Vector3((float)x1, (float)y1, (float)z1));

		float i0 = Utils::CubicLerp(n0, n1, p.z - z0);
		float i1 = Utils::CubicLerp(n2, n3, p.z - z0);
		float i2 = Utils::CubicLerp(i0, i1, p.y - y0);
		float i3 = Utils::CubicLerp(n4, n5, p.z - z0);
		float i4 = Utils::CubicLerp(n6, n7, p.z - z0);
		float i5 = Utils::CubicLerp(i3, i4, p.y - y0);

		return Utils::CubicLerp(i2, i5, p.x - x0);
	}

	static float PerlinFbm(float x, float y, float freq, int octave)
	{
		float amp = 1.0f;
		float noise = 0.0f;
		float aFactor = exp2(-0.85f);

		for (int i = 0; i < octave; ++i) {
			noise += amp * GetPerlinNoiseFbm(x * freq, y * freq);

			freq *= 2.0f;
			amp *= aFactor;
		}

		return noise;
	}

	static float PerlinFbm(float x, float y, float z, float freq, int octave)
	{
		float amp = 1.0f;
		float noise = 0.0f;
		float aFactor = exp2(-0.85f);

		for (int i = 0; i < octave; ++i) {
			noise += amp * GetPerlinNoiseFbm(x * freq, y * freq, z * freq);

			freq *= 2.0f;
			amp *= aFactor;
		}

		return noise;
	}

	static float SplineContinentalness(float value)
	{
		value = std::clamp(value * 1.5f, -1.0f, 1.0f);

		if (value <= -0.51f) {
			float w = (value - -1.0f) / (-0.51f - -1.0f);
			return Utils::CubicLerp(0.0f, 0.14f, w);
		}
		else if (value <= -0.25f) {
			float w = (value - -0.51f) / (-0.25f - -0.51f);
			return Utils::CubicLerp(0.14f, 0.31f, w);
		}
		else if (value <= -0.10f) {
			float w = (value - -0.25f) / (-0.10f - -0.25f);
			return Utils::CubicLerp(0.31f, 0.43f, w);
		}
		else if (value <= 0.09f) {
			float w = (value - -0.10f) / (0.09f - -0.10f);
			return Utils::CubicLerp(0.43f, 0.57f, w);
		}
		else if (value <= 0.42f) {
			float w = (value - 0.09f) / (0.42f - 0.09f);
			return Utils::CubicLerp(0.57f, 0.92f, w);
		}
		else {
			float w = (value - 0.42f) / (1.0f - 0.42f);
			return Utils::CubicLerp(0.92f, 1.0f, w);
		}
	}

	static float SplineErosion(float value)
	{
		value = std::clamp(value * 1.5f, -1.0f, 1.0f);

		if (value <= -0.78f) { // 0.02
			float w = (value - -1.0f) / (-0.78f - -1.0f);
			return Utils::CubicLerp(0.0f, 0.02f, w);
		}
		else if (value <= -0.57f) { // 0.14
			float w = (value - -0.78f) / (-0.57f - -0.78f);
			return Utils::CubicLerp(0.02f, 0.14f, w);
		}
		else if (value <= -0.36f) { // 0.29
			float w = (value - -0.57f) / (-0.36f - -0.57f);
			return Utils::CubicLerp(0.14f, 0.29f, w);
		}
		else if (value <= 0.03f) { // 0.43
			float w = (value - -0.36f) / (0.03f - -0.36f);
			return Utils::CubicLerp(0.29f, 0.43f, w);
		}
		else if (value <= 0.32f) { // 0.57
			float w = (value - 0.03f) / (0.32f - 0.03f);
			return Utils::CubicLerp(0.43f, 0.57f, w);
		}
		else if (value <= 0.39f) { // 0.71
			float w = (value - 0.32f) / (0.39f - 0.32f);
			return Utils::CubicLerp(0.57f, 0.71f, w);
		}
		else if (value <= 0.78f) { // 0.86
			float w = (value - 0.39f) / (0.78f - 0.39f);
			return Utils::CubicLerp(0.71f, 0.86f, w);
		}
		else { // 1.0
			float w = (value - 0.78f) / (1.0f - 0.78f);
			return Utils::CubicLerp(0.86f, 1.0f, w);
		}
	}

	static float SplinePeaksValley(float value)
	{
		value = std::clamp(abs(value * 1.5f), 0.0f, 1.0f);

		if (value <= 0.05f) {
			float w = (value - 0.0f) / (0.05f - 0.0f);
			return Utils::CubicLerp(0.01f, 0.07f, w);
		}
		else if (value <= 0.10f) {
			float w = (value - 0.05f) / (0.10f - 0.05f);
			return Utils::CubicLerp(0.07f, 0.12f, w);
		}
		else if (value <= 0.24f) {
			float w = (value - 0.10f) / (0.24f - 0.10f);
			return Utils::CubicLerp(0.12f, 0.32f, w);
		}
		else if (value <= 0.36f) {
			float w = (value - 0.24f) / (0.36f - 0.24f);
			return Utils::CubicLerp(0.32f, 0.52f, w);
		}
		else if (value <= 0.42f) {
			float w = (value - 0.36f) / (0.42f - 0.36f);
			return Utils::CubicLerp(0.52f, 0.64f, w);
		}
		else if (value <= 0.80f) {
			float w = (value - 0.42f) / (0.80f - 0.42f);
			return Utils::CubicLerp(0.64f, 1.0f, w);
		}
		else {
			float w = (value - 0.80f) / (1.0f - 0.80f);
			return Utils::CubicLerp(0.86f, 1.0f, 1.0f - w);
		}
	}

	static float GetContinentalness(int x, int z)
	{

		float scale = 1024.0f;

		float cNoise = PerlinFbm(x / scale, z / scale, 2.0f, 6);
		float cValue = SplineContinentalness(cNoise);

		if (cValue <= 0.1f)
			cValue = cValue / 0.1f - 1.0f; // [-1.0f, 0.0f]
		else
			cValue = (cValue - 0.1f) / 0.9f; // [0.0f, 1.0f]

		return cValue;
	}

	static float GetErosion(int x, int z)
	{
		float scale = 1024.0f;
		float seed = 123.0f;

		float eNoise = PerlinFbm(x / scale + seed, z / scale + seed, 2.0f, 6);
		float eValue = SplineErosion(eNoise);

		return eValue;
	}

	static float GetPeaksValley(int x, int z)
	{
		float scale = 512.0f;
		float seed = 4.0f;

		float pvNoise = PerlinFbm(x / scale + seed, z / scale + seed, 1.5f, 6);
		float pvValue = SplinePeaksValley(pvNoise);

		pvValue = (pvValue - 0.5f) * 2.0f;

		return pvValue;
	}

	static float GetElevation(float c, float e, float pv)
	{
		float elevation = 64.0f + 64.0f * c * (1.0f - e) + 64.0f * pv * powf((1.0f - e), 1.25f);

		return max(elevation, 1.0f);
	}

	static bool IsCave(int x, int y, int z)
	{
		float threshold = 0.004f;

		float density1 = PerlinFbm(x / 256.0f, y / 256.0f, z / 256.0f, 2.0f, 4);
		if (density1 * density1 > threshold)
			return false; // ealry return

		float density2 =
			PerlinFbm(x / 512.0f + 123.0f, y / 256.0f + 123.0f, z / 512.0f + 123.0f, 2.0f, 4);
		if (density2 * density2 > threshold)
			return false;

		return (density1 * density1 + density2 * density2 <= threshold);
	}

	static float GetTemperature(int x, int z)
	{
		float scale = 1024.0f;
		float seed = 157.0f;

		float tNoise = PerlinFbm(x / scale + seed, z / scale + seed, 2.0f, 6);
		tNoise = std::clamp(tNoise * 1.5f, -1.0f, 1.0f);

		float tValue = (tNoise + 1.0f) * 0.5f;
		return tValue;
	}

	static float GetHumidity(int x, int z)
	{
		float scale = 2048.0f;
		float seed = 653.0f;

		float hNoise = PerlinFbm(x / scale + seed, z / scale + seed, 2.0f, 6);
		hNoise = std::clamp(hNoise * 1.5f, -1.0f, 1.0f);

		float hValue = (hNoise + 1.0f) * 0.5f;
		return hValue;
	}

	static float GetDistribution(int x, int z)
	{
		float scale = 24.0f;
		float seed = 773.0f;

		float dNoise = PerlinFbm(x / scale + seed, z / scale + seed, 2.0f, 4);
		dNoise = std::clamp(dNoise * 1.5f, -1.0f, 1.0f);

		float dValue = (dNoise + 1.0f) * 0.5f;
		return dValue;
	}

	static float GetDensity(int x, int y, int z)
	{
		float scale = 16.0f;
		float seed = 331.0f;

		float dNoise = PerlinFbm(x / scale + seed, y / scale + seed, z / scale + seed, 2.0f, 2);

		dNoise = std::clamp(dNoise * 1.5f, -1.0f, 1.0f);

		return (dNoise + 1.0f) * 0.5f;
	}

	static void GenerateRandomPlace2D(Vector3 worldPosition, uint32_t soltX, uint32_t soltZ,
		int maxPlaceCount, int indexCount, std::vector<std::pair<int, int>>& outRandomPlace2D)
	{
		uint32_t seedX = (uint32_t)floor(worldPosition.x) + soltX;
		uint32_t seedZ = (uint32_t)floor(worldPosition.z) + soltZ;

		uint32_t soltY = (uint32_t)floor(worldPosition.y);

		for (int i = 0; i < maxPlaceCount; ++i) {
			seedX = HashInt(seedX, soltX * soltY);
			seedZ = HashInt(seedZ, soltZ * soltY);

			int xIndex = seedX % indexCount;
			int zIndex = seedZ % indexCount;

			outRandomPlace2D.push_back(std::make_pair(xIndex, zIndex));
		}
	}
}