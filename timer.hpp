

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>

#include "timestamp.hpp"


#ifndef TIMER_HPP_
#define TIMER_HPP_


namespace Piper
{

	/**
	 * Timer implements a mechanism for tracking time progress.
	 *
	 * A timer accepts period as a parameter. When a timer is started, it will
	 * receive one tick per period. The accumulate method and its friends can
	 * be used to update the tick counter to include the accumulated ticks. On
	 * the other hand, accumulated ticks can be cleared via the consume or
	 * clear method.
	 *
	 * Implementation Details
	 * ======================
	 *
	 * The timer is essentially a thin wrapper around the Linux timerfd facility.
	 * Please refer to respective manpages for details.
	 *
	 * Issues
	 * ======
	 *
	 * The use of Linux specific API makes the software Linux-specific. However,
	 * the project already depends on ALSA which is Linux only, so nothing is
	 * lost due to the choice. On the other hand, the Linux timerfd API is much
	 * more applicable to the project than the POSIX timer API due to possibliity
	 * to poll(2) on the timer.
	 *
	 * Another issue is possible wrap-around for the tick counter. The situation
	 * is deemed virtually impossible in the use case.
	 *
	 */
	class Timer
	{
		public:

			/**
			 * Construct a new timer with the given period.
			 */
			Timer(Duration period);

			/**
			 * Destroy the timer.
			 */
			~Timer();

			/**
			 * Return the descriptor of the timer.
			 */
			int descriptor() const noexcept { return m_descriptor; }

			/**
			 * Return the period of the timer.
			 */
			Duration period() const noexcept { return m_period; }

			/**
			 * Return the number of ticks accumulated by the timer.
			 */
			unsigned int ticks() const noexcept { return m_ticks; }

			/**
			 * Start the timer and clear accumulated ticks. Throws timer exception
			 * when the timer cannot be started.
			 */
			void start();

			/**
			 * Stop the timer and clear accumulated ticks. Throws timer exception
			 * when the timer cannot be stopped.
			 */
			void stop();

			/**
			 * Clear all accumulated ticks and return the amount of ticks cleared.
			 */
			unsigned int consume();

			/**
			 * Clear all accumulated ticks.
			 */
			void clear();

			/**
			 * Wait for incoming ticks and accumulate them in the timer. The method
			 * will return only when new ticks are accumulated.
			 */
			void accumulate();

			/**
			 * Attempt to wait for incoming ticks and accumulate them in the timer.
			 * This method will return only when:
			 *
			 * 1. New ticks are acuumulated; or
			 * 2. The process receives POSIX signal.
			 *
			 * This call is equivalent to calling the `try_accumulate(int)` method
			 * with timeout -1.
			 */
			void try_accumulate();

			/**
			 * Attempt to wait for incoming ticks and accumulate them in the timer.
			 * This method will return only when:
			 *
			 * 1. New ticks are acuumulated; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
			 * Note that the timeout accepts 2 special values. The timeout of 0 means
			 * no waiting. The timeout of -1 indicates that timeout is not observed.
			 */
			void try_accumulate(int timeout);

			Timer(const Timer& bucket) = delete;
			Timer(Timer&& bucket) = delete;
			Timer& operator=(const Timer& bucket) = delete;
			Timer& operator=(Timer&& bucket) = delete;

		private:

			/**
			 * Descriptor of the timerfd.
			 */
			int m_descriptor;

			/**
			 * Period of the timer.
			 */
			Duration m_period;

			/**
			 * Number of ticks accumulated so far.
			 */
			unsigned int m_ticks;

			/**
			 * Buffer for storing overrun count read from the timerfd descriptor.
			 */
			std::uint64_t m_overrun;

			/**
			 * Pointer to the overrun buffer where data can be read into. Normally
			 * it will point to nothing. However, while the timer is checking for
			 * accumulated ticks, the pointer should point to `m_overrun` or the
			 * following 7 bytes (`m_overrun` should be 8 byte long).
			 */
			char* m_destination;

			/**
			 * Number of bytes read into the overrun buffer.
			 */
			std::size_t m_consumed;

			/**
			 * Number of bytes to be read into the overrun buffer before it is
			 * completed.
			 */
			std::size_t m_remainder;
	};

	/**
	 * Exception indicating problem with timer. A token bucket employs an
	 * internal timer to determine the elapsed time and the amount of tokens
	 * to replenish. This exception represents some errors with that timer.
	 */
	class TimerException : public std::runtime_error
	{
		public:
			using std::runtime_error::runtime_error;
	};

}


#endif


