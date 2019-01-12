

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>

#include "exception.hpp"
#include "timestamp.hpp"
#include "timer.hpp"
#include "tokenbucket.hpp"


#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "tokenbucket.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "tokenbucket.cpp", __LINE__);


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
			EXC_START(std::invalid_argument("[Piper::TokenBucket::TokenBucket] Cannot create token bucket due to invalid capacity"));
		} else if (m_fill == 0) {
			EXC_START(std::invalid_argument("[Piper::TokenBucket::TokenBucket] Cannot create token bucket due to invalid fill"));
		}
	}

	void TokenBucket::start()
	{
		try {
			m_timer.start();
			m_tokens = 0;
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::TokenBucket::start] Cannot start token bucket due to logic error in underlying component"));
		} catch (TimerException& ex) {
			EXC_CHAIN(TokenBucketException("[Piper::TokenBucket::start] Cannot start token bucket due to timer error"));
		}
	}

	void TokenBucket::stop()
	{
		try {
			m_timer.stop();
			m_tokens = 0;
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::TokenBucket::stop] Cannot stop token bucket due to logic error in underlying component"));
		} catch (TimerException& ex) {
			EXC_CHAIN(TokenBucketException("[Piper::TokenBucket::stop] Cannot stop token bucket due to timer error"));
		}
	}

	void TokenBucket::spend(unsigned int amount)
	{
		if (m_tokens >= amount) {
			m_tokens -= amount;
		} else {
			EXC_START(std::invalid_argument("[Piper::TokenBucket::spend] Cannot spend tokens due to overspend"));
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
				try {
					m_timer.try_accumulate(timeout);
				} catch (std::invalid_argument& ex) {
					EXC_CHAIN(std::logic_error("[Piper::TokenBucket::start] Cannot start token bucket due to invalid argument to underlying component"));
				} catch (std::logic_error& ex) {
					EXC_CHAIN(std::logic_error("[Piper::TokenBucket::try_refill] Cannot refill token bucket due to logic error in underlying component"));
				} catch (TimerException& ex) {
					EXC_CHAIN(TokenBucketException("[Piper::TokenBucket::try_refill] Cannot refill token bucket due to timer error"));
				}
			}

			if (m_timer.ticks() > 0) {
				unsigned int increment = m_timer.consume() * m_fill;
				unsigned int limit = m_capacity - m_tokens;
				m_tokens += std::min(increment, limit);
			}
		}
	}

};


