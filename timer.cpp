

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <cstring>
#include <exception>
#include <stdexcept>
#include <system_error>

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "timer.hpp"


#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "timer.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "timer.cpp", __LINE__);
#define EXC_SYSTEM(err) std::system_error(err, std::system_category(), strerror(err))


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
			switch (errno) {
				case EINVAL: EXC_START(std::logic_error("[Piper::Timer::Timer] Cannot create timer due to unexpected error on clockid or flags"));
				default: EXC_START(EXC_SYSTEM(errno), TimerException("Cannot create timer due to operating system error"));
			}
		} else if (m_period == 0) {
			EXC_START(std::invalid_argument("[Piper::Timer::Timer] period cannot be zero"));
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
		interval.it_value.tv_sec = static_cast<time_t>(m_period / 1000000000);
		interval.it_value.tv_nsec = static_cast<long>(m_period % 1000000000);
		interval.it_interval.tv_sec = static_cast<time_t>(m_period / 1000000000);
		interval.it_interval.tv_nsec = static_cast<long>(m_period % 1000000000);

		if (::timerfd_settime(m_descriptor, 0, &interval, nullptr) >= 0) {
			m_ticks = 0;
		} else {
			switch (errno) {
				case EBADF: EXC_START(std::logic_error("[Piper::Timer::start] Cannot start timer due to stale descriptor"));
				case EINVAL: EXC_START(std::logic_error("[Piper::Timer::start] Cannot start timer due to stale descriptor"));
				case EFAULT: EXC_START(std::logic_error("[Piper::Timer::start] Cannot start timer due to unexpected error on itimerspec pointers"));
				default: EXC_START(EXC_SYSTEM(errno), TimerException("[Piper::Timer::start] Cannot start timer due to operating system error"));
			}
		}
	}

	void Timer::stop()
	{
		struct itimerspec interval;
		interval.it_value.tv_sec = 0;
		interval.it_value.tv_nsec = 0;
		interval.it_interval.tv_sec = 0;
		interval.it_interval.tv_nsec = 0;

		if (::timerfd_settime(m_descriptor, 0, &interval, nullptr) >= 0) {
			m_ticks = 0;
		} else {
			switch (errno) {
				case EBADF: EXC_START(std::logic_error("[Piper::Timer::stop] Cannot stop timer due to stale descriptor"));
				case EINVAL: EXC_START(std::logic_error("[Piper::Timer::stop] Cannot stop timer due to stale descriptor"));
				case EFAULT: EXC_START(std::logic_error("[Piper::Timer::stop] Cannot stop timer due to unexpected error on itimerspec pointers"));
				default: EXC_START(EXC_SYSTEM(errno), TimerException("[Piper::Timer::stop] Cannot stop timer due to operating system error"));
			}
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
					m_consumed += static_cast<std::size_t>(received);
					m_remainder -= static_cast<std::size_t>(received);

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

					if (errno == EBADF) {
						EXC_START(std::logic_error("Piper::Timer::try_accumulate] Cannot check timer due to stale descriptor"));
					} else {
						EXC_START(EXC_SYSTEM(errno), TimerException("[Piper::Timer::try_accumulate] Cannot check timer due to operating system error"));
					}
				}
			} else if (available < 0 && errno != EINTR) {
				m_overrun = 0;
				m_destination = nullptr;
				m_consumed = 0;
				m_remainder = sizeof(m_overrun);
				EXC_START(EXC_SYSTEM(errno), TimerException("[Piper::Timer::try_accumulate] Cannot check timer due to operating system error"));
			}
		}
	}

}


