

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <algorithm>

#include "exception.hpp"
#include "timestamp.hpp"
#include "timer.hpp"
#include "tokenbucket.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Token bucket implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	TokenBucket::TokenBucket(unsigned int capacity, unsigned int fill, Duration period) :
		m_timer(period),
		m_capacity(capacity),
		m_fill(fill),
		m_tokens(0)
	{
		if (m_capacity == 0) {
			throw InvalidArgumentException("invalid capacity", "tokenbucket.cpp", __LINE__);
		} else if (m_fill == 0) {
			throw InvalidArgumentException("invalid fill", "tokenbucket.cpp", __LINE__);
		}
	}

	void TokenBucket::start()
	{
		m_timer.start();
		m_tokens = 0;
	}

	void TokenBucket::stop()
	{
		m_timer.stop();
		m_tokens = 0;
	}

	void TokenBucket::spend(unsigned int amount)
	{
		if (m_tokens >= amount) {
			m_tokens -= amount;
		} else {
			throw InvalidArgumentException("invalid amount", "tokenbucket.cpp", __LINE__);
		}
	}

	void TokenBucket::refill()
	{
		while (m_tokens == 0) {
			try_refill(-1);
		}
	}

	void TokenBucket::try_refill()
	{
		return try_refill(-1);
	}

	void TokenBucket::try_refill(int timeout)
	{
		if (m_tokens == 0) {
			if (m_timer.ticks() == 0) {
				m_timer.try_accumulate(timeout);
			}

			if (m_timer.ticks() > 0) {
				unsigned int increment = m_timer.consume() * m_fill;
				unsigned int limit = m_capacity - m_tokens;
				m_tokens += std::min(increment, limit);
			}
		}
	}

};


