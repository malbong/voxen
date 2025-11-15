#include "Instance.h"

InstanceTypeInfoSet Instance::m_instanceTypeInfoSet;

INSTANCE_SHAPE Instance::GetShape(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetShape();
}

TEXTURE_INDEX Instance::GetTextureIndex(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetTextureIndex();
}