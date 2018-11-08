


#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include "exception.hpp"
#include "tokenbucket.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Refill Operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	inline TokenBucket::Refill::Refill(const TokenBucket& bucket) :
		m_bucket(bucket),
		m_overrun(0),
		m_start(reinterpret_cast<char*>(&m_overrun)),
		m_remainder(sizeof(m_overrun))
	{
		// empty
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Token bucket implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	TokenBucket::TokenBucket(unsigned int capacity, unsigned int fill, unsigned int period) :
		m_timerfd(::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)),
		m_capacity(capacity),
		m_fill(fill),
		m_period(period),
		m_tokens(0)
	{
		if (m_capacity == 0) {
			throw InvalidArgumentException("invalid capacity", "tokenbucket.cpp", __LINE__);
		} else if (m_fill == 0) {
			throw InvalidArgumentException("invalid fill", "tokenbucket.cpp", __LINE__);
		} else if (m_period == 0) {
			throw InvalidArgumentException("invalid period", "tokenbucket.cpp", __LINE__);
		} else if (m_timerfd < 0) {
			throw TimerException("cannot create timer", "tokenbucket.cpp", __LINE__);
		}
	}

	TokenBucket::~TokenBucket()
	{
		if (m_timerfd >= 0) {
			::close(m_timerfd);
		}
	}

	void TokenBucket::start()
	{
		struct itimerspec interval;
		interval.it_value.tv_sec = 0;
		interval.it_value.tv_nsec = ((long) m_period) * 1000000L;
		interval.it_interval.tv_sec = 0;
		interval.it_interval.tv_nsec = ((long) m_period) * 1000000L;

		if (::timerfd_settime(m_timerfd, CLOCK_MONOTONIC, &interval, NULL) >= 0) {
			m_tokens = 0;
		} else {
			throw TimerException("cannot start timer", "tokenbucket.cpp", __LINE__);
		}
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
		TokenBucket::Refill operation = refill_async();
		while (done(operation) == false) {
			execute(operation);
		}
	}

	TokenBucket::Refill TokenBucket::refill_async()
	{
		return TokenBucket::Refill(*this);
	}

	bool TokenBucket::done(TokenBucket::Refill& operation)
	{
		if (this == &operation.m_bucket) {
			return operation.m_remainder == 0;
		} else {
			throw InvalidArgumentException("invalid operation", "tokenbucket.cpp", __LINE__);
		}
	}

	void TokenBucket::execute(TokenBucket::Refill& operation)
	{
		if (this == &operation.m_bucket) {
			if (operation.m_remainder > 0) {
				size_t received = ::read(m_timerfd, operation.m_start, operation.m_remainder);

				if (received > 0) {
					operation.m_start += received;
					operation.m_remainder -= received;
				} else if (received == 0) {
					throw TimerException("cannot check timer", "tokenbucket.cpp", __LINE__);
				} else if (received < 0 && errno != EINTR) {
					throw TimerException("cannot check timer", "tokenbucket.cpp", __LINE__);
				}

				if (operation.m_remainder == 0) {
					unsigned int increment = operation.m_overrun * m_fill;
					unsigned int limit = m_capacity - m_tokens;
					m_tokens += std::min(increment, limit);
				}
			}
		} else {
			throw InvalidArgumentException("invalid operation", "tokenbucket.cpp", __LINE__);
		}
	}

	void TokenBucket::stop()
	{
		struct itimerspec interval;

		interval.it_value.tv_sec = 0;
		interval.it_value.tv_nsec = 0;
		interval.it_interval.tv_sec = 0;
		interval.it_interval.tv_nsec = 0;

		if (::timerfd_settime(m_timerfd, CLOCK_MONOTONIC, &interval, NULL) >= 0) {
			m_tokens = 0;
		} else {
			throw TimerException("cannot stop timer", "tokenbucket.cpp", __LINE__);
		}
	}

};


