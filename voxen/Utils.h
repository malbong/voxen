#pragma once

#include <directxtk/SimpleMath.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <stb_image.h>
#include <algorithm>

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
};