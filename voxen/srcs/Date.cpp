#include "Date.h"
#include "DXUtils.h"

Date::Date() : m_days(0), m_iDateTime(0), m_fDateTime(0.0f) {}

Date::~Date() {}

bool Date::Initialize() {
	m_constantData.days = m_days;
	m_constantData.dateTime = m_iDateTime;
	m_constantData.dayCycleRealTime = DAY_CYCLE_REAL_TIME;
	m_constantData.dayCycleAmount = DAY_CYCLE_AMOUNT;

	if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
		std::cout << "failed create date constant buffer" << std::endl;
		return false;
	}

	return true;
}

void Date::Update(float dt) {
	m_fDateTime += DAY_CYCLE_TIME_SPEED * dt;

	if (m_fDateTime > DAY_CYCLE_AMOUNT) {
		m_days++;
		m_fDateTime -= DAY_CYCLE_AMOUNT;
	}

	m_iDateTime = (UINT)m_fDateTime;

	m_constantData.days = m_days;
	m_constantData.dateTime = m_iDateTime;
	DXUtils::UpdateConstantBuffer(m_constantBuffer, m_constantData);
}