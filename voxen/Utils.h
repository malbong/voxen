#pragma once

#include <directxtk/SimpleMath.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <stb_image.h>
#include <algorithm>

#include "Pos.h"

using namespace DirectX::SimpleMath;

namespace Utils {
	static const float PI = 3.14159265f;
	static const float invPI = 1.0f / PI;

	static inline int Signf(float x) { return (x > 0) - (x < 0); }

	static inline float Sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

	static Vector3 SRGB2Linear(Vector3 color)
	{
		float r = std::pow(std::clamp(color.x, 0.0f, 1.0f), 2.2f);
		float g = std::pow(std::clamp(color.y, 0.0f, 1.0f), 2.2f);
		float b = std::pow(std::clamp(color.z, 0.0f, 1.0f), 2.2f);

		return Vector3(r, g, b);
	}

	static inline int WrapToBase(int x, int baseSize)
	{ 
		return ((x % baseSize) + baseSize) % baseSize;
	}

	static Vector3 CalcOffsetPos(Vector3 pos, int baseSize)
	{
		int floorX = (int)floor(pos.x);
		int floorY = (int)floor(pos.y);
		int floorZ = (int)floor(pos.z);

		int modX = WrapToBase(floorX, baseSize);
		int modY = WrapToBase(floorY, baseSize);
		int modZ = WrapToBase(floorZ, baseSize);

		return Vector3((float)(floorX - modX), (float)(floorY - modY), (float)(floorZ - modZ));
	}

	template <typename T> inline static T Lerp(T a, T b, float w)
	{
		w = std::clamp(w, 0.0f, 1.0f);
		return (1 - w) * a + w * b;
	}

	inline static float CubicLerp(float a, float b, float w)
	{
		w = std::clamp(w, 0.0f, 1.0f);
		return (b - a) * (float)((3.0f - w * 2.0f) * w * w) + a;
	}

	inline static float Smootherstep(float a, float b, float w)
	{
		return (b - a) * (float)((w * (w * 6.0 - 15.0) + 10.0) * w * w * w) + a;
	}

	static void ReadImage(
		const std::string filename, std::vector<uint8_t>& image, int& width, int& height)
	{

		int channels = 4;
		unsigned char* img = stbi_load(filename.c_str(), &width, &height, &channels, 0);

		image.resize((size_t)width * height * 4);

		if (channels == 1) {
			for (size_t i = 0; i < (size_t)width * height; i++) {
				uint8_t g = img[i * channels + 0];
				for (size_t c = 0; c < 4; c++) {
					image[4 * i + c] = g;
				}
			}
		}
		else if (channels == 2) {
			for (size_t i = 0; i < (size_t)width * height; i++) {
				for (size_t c = 0; c < 2; c++) {
					image[4 * i + c] = img[i * channels + c];
				}
				image[4 * i + 2] = 255;
				image[4 * i + 3] = 255;
			}
		}
		else if (channels == 3) {
			for (size_t i = 0; i < (size_t)width * height; i++) {
				for (size_t c = 0; c < 3; c++) {
					image[4 * i + c] = img[i * channels + c];
				}
				image[4 * i + 3] = 255;
			}
		}
		else if (channels == 4) {
			for (size_t i = 0; i < (size_t)width * height; i++) {
				for (size_t c = 0; c < 4; c++) {
					image[4 * i + c] = img[i * channels + c];
				}
			}
		}
		else {
			std::cout << "Cannot read " << channels << " channels" << std::endl;
		}

		delete[] img;
	}

	static inline int GetIndexFrom3D(int axis, int y, int x, int length)
	{
		return (length * length) * axis + length * y + x;
	}

	static inline int TrailingZeros(uint64_t num)
	{
		if (num == 0)
			return 64;
		return (int)log2(num & ((~num) + 1)); // __builtin_ctzll or _BitScanForward64
	}

	static inline int TrailingOnes(uint64_t num)
	{
		return (int)log2((num & ~(num + 1)) + 1); // __builtin_ctzll or _BitScanForward64}
	}

	static inline PosInt3 VectorToPosInt3(Vector3 v)
	{
		return PosInt3((int)v.x, (int)v.y, (int)v.z);
	}

	static inline Vector3 PosInt3ToVector(PosInt3 t) 
	{ 
		return Vector3((float)std::get<0>(t), (float)std::get<1>(t), (float)std::get<2>(t));
	}

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

	static int RandomRangeByPos(PosInt3 pos, int min, int max)
	{
		int x = std::get<0>(pos);
		int y = std::get<1>(pos);
		int z = std::get<2>(pos);

		uint32_t length = max - min + 1;

		uint32_t hash = HashInt(x * y * z, 100057u);

		return min + hash % length;
	}

	static int RandomRangeByPos(PosInt3 pos, uint32_t seed, int min, int max)
	{
		int x = std::get<0>(pos);
		int y = std::get<1>(pos);
		int z = std::get<2>(pos);

		uint32_t length = max - min + 1;

		uint32_t hash = HashInt(x * y * z, seed);

		return min + hash % length;
	}

	static int RandomRangeByPosForLoop(PosInt3 pos, int loop, int min, int max)
	{
		int x = std::get<0>(pos);
		int y = std::get<1>(pos);
		int z = std::get<2>(pos);

		uint32_t length = max - min + 1;

		uint32_t hash = HashInt(x * y * z, 100057u);
		for (int i = 0; i < loop; ++i) {
			hash = HashInt(hash, 100057u);
		}

		return min + hash % length;
	}

	static int RandomRangeByPosForLoop(PosInt3 pos, int loop, uint32_t seed, int min, int max)
	{
		int x = std::get<0>(pos);
		int y = std::get<1>(pos);
		int z = std::get<2>(pos);

		uint32_t length = max - min + 1;

		uint32_t hash = HashInt(x * y * z, seed);
		for (int i = 0; i < loop; ++i) {
			hash = HashInt(hash, seed);
		}

		return min + hash % length;
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

		float i0 = CubicLerp(n0, n1, p.x - (float)x0);
		float i1 = CubicLerp(n2, n3, p.x - (float)x0);

		return CubicLerp(i0, i1, p.y - (float)y0);
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

		float i0 = CubicLerp(n0, n1, p.z - z0);
		float i1 = CubicLerp(n2, n3, p.z - z0);
		float i2 = CubicLerp(i0, i1, p.y - y0);
		float i3 = CubicLerp(n4, n5, p.z - z0);
		float i4 = CubicLerp(n6, n7, p.z - z0);
		float i5 = CubicLerp(i3, i4, p.y - y0);

		return CubicLerp(i2, i5, p.x - x0);
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
};