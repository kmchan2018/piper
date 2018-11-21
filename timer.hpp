

#include <cstddef>
#include <cstdint>

#include "exception.hpp"


#ifndef TIMER_H_
#define TIMER_H_


namespace Piper
{

	/**
	 * Timer implements a mechanism for tracking time progress.
	 */
	class Timer
	{
		public:

			/**
			 * Construct a new timer with the given period.
			 */
			Timer(unsigned int period);

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
			unsigned int period() const noexcept { return m_period; }

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
			int m_descriptor;
			unsigned int m_period;
			unsigned int m_ticks;

			std::uint64_t m_overrun;
			char* m_destination;
			unsigned int m_consumed;
			unsigned int m_remainder;
	};

	/**
	 * Exception indicating problem with timer. A token bucket employs an
	 * internal timer to determine the elapsed time and the amount of tokens
	 * to replenish. This exception represents some errors with that timer.
	 */
	class TimerException : public Exception
	{
		public:
			using Exception::Exception;
	};

};


#endif


