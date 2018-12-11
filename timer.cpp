

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "timer.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Timer implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Timer::Timer(Duration period) :
		m_descriptor(::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)),
		m_period(period),
		m_ticks(0),
		m_overrun(0),
		m_destination(nullptr),
		m_consumed(0),
		m_remainder(sizeof(m_destination))
	{
		if (m_descriptor < 0) {
			throw TimerException("cannot create timer", "timer.cpp", __LINE__);
		} else if (m_period == 0) {
			throw InvalidArgumentException("invalid period", "timer.cpp", __LINE__);
		}
	}

	Timer::~Timer()
	{
		if (m_descriptor >= 0) {
			::close(m_descriptor);
		}
	}

	void Timer::start()
	{
		struct itimerspec interval;
		interval.it_value.tv_sec = m_period / 1000000000;
		interval.it_value.tv_nsec = m_period % 1000000000;
		interval.it_interval.tv_sec = m_period / 1000000000;
		interval.it_interval.tv_nsec = m_period % 1000000000;

		if (::timerfd_settime(m_descriptor, 0, &interval, NULL) >= 0) {
			m_ticks = 0;
		} else {
			throw TimerException("cannot start timer", "timer.cpp", __LINE__);
		}
	}

	void Timer::stop()
	{
		struct itimerspec interval;
		interval.it_value.tv_sec = 0;
		interval.it_value.tv_nsec = 0;
		interval.it_interval.tv_sec = 0;
		interval.it_interval.tv_nsec = 0;

		if (::timerfd_settime(m_descriptor, 0, &interval, NULL) >= 0) {
			m_ticks = 0;
		} else {
			throw TimerException("cannot stop timer", "timer.cpp", __LINE__);
		}
	}

	unsigned int Timer::consume()
	{
		unsigned int ticks = m_ticks;
		m_ticks = 0;
		return ticks;
	}

	void Timer::clear()
	{
		m_ticks = 0;
	}

	void Timer::accumulate()
	{
		while (m_ticks == 0) {
			try_accumulate(-1);
		}
	}

	void Timer::try_accumulate()
	{
		return try_accumulate(-1);
	}

	void Timer::try_accumulate(int timeout)
	{
		if (m_ticks == 0) {
			struct pollfd pollfd;
			ssize_t received;
			int available;

			if (m_destination == nullptr) {
				m_destination = reinterpret_cast<char*>(&m_overrun);
				m_consumed = 0;
				m_remainder = sizeof(m_overrun);
			}

			pollfd.fd = m_descriptor;
			pollfd.events = POLLIN;
			pollfd.revents = 0;

			available = ::poll(&pollfd, 1, timeout);

			if (available > 0) {
				received = ::read(m_descriptor, m_destination, m_remainder);

				if (received > 0) {
					m_destination += received;
					m_consumed += received;
					m_remainder -= received;

					if (m_remainder == 0) {
						m_ticks += m_overrun;
						m_overrun = 0;
						m_destination = nullptr;
						m_consumed = 0;
						m_remainder = sizeof(m_overrun);
					}
				} else if (received < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
					m_overrun = 0;
					m_destination = nullptr;
					m_consumed = 0;
					m_remainder = sizeof(m_overrun);
					throw TimerException("cannot check timer", "timer.cpp", __LINE__);
				}
			} else if (available < 0 && errno != EINTR) {
				m_overrun = 0;
				m_destination = nullptr;
				m_consumed = 0;
				m_remainder = sizeof(m_overrun);
				throw TimerException("cannot poll timer", "timer.cpp", __LINE__);
			}
		}
	}

};


