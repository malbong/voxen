#include "Block.h"
#include "Terrain.h"
#include "Biome.h"


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

BLOCK_TYPE Block::GetBlockTypeForInner(int x, int y, int z, float distribution)
{
	BLOCK_TYPE blockType;

	float density = Terrain::GetDensity(x, y, z);
	if (density <= 0.3f) {
		blockType = BLOCK_DIRT;
	}
	else if (density <= 0.75f) {
		blockType = BLOCK_STONE;
	}
	else if (density <= 0.88f) {
		blockType = BLOCK_ANDESITE;
	}
	else {
		if (distribution <= 0.4f) {
			blockType = BLOCK_COAL_ORE;
		}
		else if (distribution <= 0.6f) {
			blockType = BLOCK_COPPER_ORE;
		}
		else if (distribution <= 0.7f) {
			blockType = BLOCK_IRON_ORE;
		}
		else if (distribution <= 0.8f) {
			blockType = BLOCK_REDSTONE_ORE;
		}
		else if (distribution <= 0.9f) {
			blockType = BLOCK_GOLD_ORE;
		}
		else {
			blockType = BLOCK_DIAMOND_ORE;
		}
	}

	return blockType;
}

BLOCK_TYPE Block::GetBlockTypeForBiome(BIOME_TYPE biomeType, int y, float h, float d)
{
	int baseHeight = (int)floor(h);

	switch (biomeType) {

	case BIOME_OCEAN:
		if (d <= 0.42f) {
			return BLOCK_SAND;
		}
		else if (d <= 0.66f) {
			return BLOCK_DIRT;
		}
		else if (d <= 0.83f) {
			return BLOCK_GRAVEL;
		}
		else {
			return BLOCK_CLAY;
		}
		break;

	case BIOME_BEACH:
		if (d <= 0.6f) {
			return BLOCK_SAND;
		}
		else if (d <= 0.75f) {
			return BLOCK_SANDSTONE;
		}
		else if (d <= 0.90f) {
			return BLOCK_GRAVEL;
		}
		else {
			return BLOCK_CLAY;
		}

	case BIOME_DESERT:
		if (d <= 0.66f) {
			return BLOCK_SAND;
		}
		else {
			return BLOCK_SANDSTONE;
		}

	case BIOME_TAIGA:
		if (y == baseHeight) { // top
			if (d <= 0.6f) {
				return BLOCK_GRASS;
			}
			else if (d <= 0.85f) {
				return BLOCK_PODZOL;
			}
			else {
				return BLOCK_COARSE;
			}
		}
		else {
			if (d <= 0.7f) {
				return BLOCK_COARSE;
			}
			else {
				return BLOCK_DIRT;
			}
		}

	case BIOME_SNOWY_TAIGA:
		if (y == baseHeight) { // top
			if (d <= 0.6f) {
				return BLOCK_SNOW_GRASS;
			}
			else if (d <= 0.88f) {
				return BLOCK_GRASS;
			}
			else {
				return BLOCK_COARSE;
			}
		}
		else {
			if (d <= 0.6f) {
				return BLOCK_COARSE;
			}
			else {
				return BLOCK_DIRT;
			}
		}

	case BIOME_TUNDRA:
		if (y == baseHeight) {
			if (d <= 0.2f) {
				return BLOCK_ICE;
			}
			else if (d <= 0.6f) {
				return BLOCK_SNOW;
			}
			else {
				return BLOCK_SNOW_GRASS;
			}
		}
		else if (y == baseHeight - 1) {
			if (d <= 0.6f) {
				return BLOCK_SNOW_GRASS;
			}
			else {
				return BLOCK_DIRT;
			}
		}
		else {
			if (d <= 0.5f) {
				return BLOCK_COARSE;
			}
			else {
				return BLOCK_DIRT;
			}
		}

	case BIOME_SWAMP:
		if (y == baseHeight) {
			if (d <= 0.25f) {
				return BLOCK_MUD;
			}
			else if (d <= 0.4f) {
				return BLOCK_MOSS;
			}
			else if (d <= 0.9f) {
				return BLOCK_GRASS;
			}
			else {
				return BLOCK_DIRT;
			}
		}
		else  {
			if (d <= 0.25f) {
				return BLOCK_STONE;
			}
			else if (d <= 0.9f) {
				return BLOCK_MOSS;
			}	
			else {
				return BLOCK_MUD;
			}
		}

	case BIOME_PLAINS:
	case BIOME_FOREST:
	case BIOME_SHRUBLAND:
	case BIOME_RAINFOREST:
	case BIOME_SEASONFOREST:
	case BIOME_SAVANNA:
		if (y == baseHeight) {
			if (d <= 0.95f) {
				return BLOCK_GRASS;
			}
			else {
				return BLOCK_DIRT;
			}
		}
		else if (y == baseHeight - 1) {
			if (d <= 0.2f)
				return BLOCK_STONE;
			else
				return BLOCK_DIRT;
		}
		else {
			return BLOCK_DIRT;
		}

	default:
		return BLOCK_BEDROCK;
	}
}

BLOCK_TYPE Block::GetBlockType(int x, int y, int z, float continentalness, float erosion,
	float peaksValley, float temperature, float humidity, float distribution, float elevation)
{
	if (y == Terrain::MIN_HEIGHT_LEVEL)
		return BLOCK_BEDROCK;

	BLOCK_TYPE blockType = BLOCK_AIR;
	if (y <= Terrain::WATER_HEIGHT_LEVEL)
		blockType = BLOCK_WATER;
	if (y == Terrain::WATER_HEIGHT_LEVEL && temperature < 0.25f)
		blockType = BLOCK_ICE;

	if (y < elevation && !Terrain::IsCave(x, y, z)) {
		int biomeLayer =
			1 + (int)(6.0f * (1.0f - erosion) * powf(((-peaksValley + 1.0f) * 0.5f), 0.5f));

		if (y <= elevation - biomeLayer) {
			blockType = GetBlockTypeForInner(x, y, z, distribution);
		}
		else {
			BIOME_TYPE biomeType =
				Biome::GetBiomeType(elevation, temperature, humidity, peaksValley, erosion);
			blockType = GetBlockTypeForBiome(biomeType, y, elevation, distribution);
		}
	}

	return blockType;
}