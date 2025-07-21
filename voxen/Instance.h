#pragma once

#include <stdint.h>
#include <directxtk/SimpleMath.h>

using namespace DirectX::SimpleMath;

class Instance {
public:
	static const int INSTANCE_TYPE_COUNT = 3;

	static inline INSTANCE_TYPE GetInstanceType(uint8_t texIndex)
	{
		// 임시 데이터
		if (128 <= texIndex && texIndex < 128 + 16)
			return INSTANCE_TYPE::INSTANCE_CROSS;
		else if (128 + 16 <= texIndex && texIndex < 128 + 16 * 2)
			return INSTANCE_TYPE::INSTANCE_FENCE;
		else if (128 + 16 * 2 <= texIndex && texIndex < 128 + 16 * 3)
			return INSTANCE_TYPE::INSTANCE_SQUARE;
		else
			return INSTANCE_TYPE::INSTANCE_NONE;
	}

	Instance() : m_world(Matrix()), m_texIndex(TEXTURE_INDEX::TEXTURE_SHORT_GRASS) {}
	~Instance() {}

	inline const Matrix& GetWorld() const { return m_world; }
	inline void SetWorld(const Matrix& world) { m_world = world; }

	inline TEXTURE_INDEX GetTextureIndex() const { return m_texIndex; }
	inline void SetTextureIndex(TEXTURE_INDEX index) { m_texIndex = index; }
		

private:
	Matrix m_world; // scale rotate position
	TEXTURE_INDEX m_texIndex;
};