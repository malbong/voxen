#include "Block.h"

BlockTypeInfoSet Block::m_blockTypeInfoSet;

bool Block::IsTransparency(BLOCK_TYPE type)
{
	return m_blockTypeInfoSet.GetInfo(type).IsTransparency();
}

bool Block::IsOpaque(BLOCK_TYPE type) { return m_blockTypeInfoSet.GetInfo(type).IsOpaque(); }

bool Block::IsSemiAlpha(BLOCK_TYPE type) { return m_blockTypeInfoSet.GetInfo(type).IsSemiAlpha(); }

uint8_t Block::GetBlockTextureIndex(BLOCK_TYPE type, uint8_t face) 
{
	return m_blockTypeInfoSet.GetInfo(type).GetTexIndex(face);
}
