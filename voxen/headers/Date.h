#pragma once

#include <d3d11.h>
#include <wrl.h>

#include "Structure.h"

using namespace Microsoft::WRL;

class Date {

public:
	static const UINT DAY_CYCLE_AMOUNT = 24000;
	static const UINT DAY_CYCLE_REAL_TIME = 30;
	static const UINT DAY_CYCLE_TIME_SPEED = DAY_CYCLE_AMOUNT / DAY_CYCLE_REAL_TIME;

	static const UINT DAY_START = 1000;
	static const UINT DAY_END = 11000;

	static const UINT MAX_SUNSET = 12500;

	static const UINT NIGHT_START = 13700;
	static const UINT NIGHT_END = 22300;

	static const UINT MAX_SUNRISE = 23500;

	Date();
	~Date();

	bool Initialize();
	void Update(float dt);

	inline UINT GetDateTime() { return m_iDateTime; }

	ComPtr<ID3D11Buffer> m_constantBuffer;

private:
	UINT m_days;
	UINT m_iDateTime;
	float m_fDateTime;

	DateConstantData m_constantData;
};