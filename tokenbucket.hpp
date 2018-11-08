

#include <cstddef>
#include <cstdint>

#include "exception.hpp"


#ifndef TOKENBUCKET_H_
#define TOKENBUCKET_H_


namespace Piper
{

	/**
	 * A token bucket is an algorithm for flow control and traffic shaping.
	 * 
	 * The idea behind a token bucket is that data traffic is coupled with
	 * consumption of tokens in a bucket and by controlling the refilling of
	 * tokens into the bucket one can control the data traffic.
	 *
	 * A token bucket has 2 parameters: bucket capacity and the token refill
	 * rate. The refill rate limits the average data rate; and the capacity
	 * controls the jitter and burstiness of the traffic.
	 *
	 * This implementation is pretty standard, except that this implementation
	 * specifies the refill rate via two parameters: `fill` and `period`.
	 * The `fill` parameter controls the amount of tokens replenished in each
	 * refill operation, and the `period` parameter controls the time period
	 * between refills in microseconds.
	 *
	 * Another thing of note is that this implementation uses Linux specific
	 * timerfd API to implement the internal timer.
	 */
	class TokenBucket
	{
		public:

			/**
			 * Information for refill operations.
			 */
			class Refill
			{
				public:
					explicit inline Refill(const TokenBucket& bucket);
				private:
					friend class TokenBucket;
					const TokenBucket& m_bucket;
					std::uint64_t m_overrun;
					char* m_start;
					std::size_t m_remainder;
			};

			/**
			 * Construct a new token bucket with the given parameters and create the
			 * necessary timer for its operation.
			 */
			TokenBucket(unsigned int capacity, unsigned int fill, unsigned int period);

			/**
			 * Destroy the token bucket and remove associated timer.
			 */
			~TokenBucket();

			/**
			 * Return the amount of tokens the token bucket can hold.
			 */
			unsigned int capacity() const noexcept { return m_capacity; }

			/**
			 * Return the amount of tokens filled in a single time period.
			 */
			unsigned int fill() const noexcept { return m_fill; }

			/**
			 * Return the duration of a single time period.
			 */
			unsigned int period() const noexcept { return m_period; }

			/**
			 * Return the number of tokens in the token bucket.
			 */
			unsigned int tokens() const noexcept { return m_tokens; }

			/**
			 * Return whether the token bucket contains token or not.
			 */
			bool filled() const noexcept { return m_tokens > 0; }

			/**
			 * Start the token bucket and the associated timer. Throws timer exception
			 * when the timer cannot be started.
			 */
			void start();

			/**
			 * Spend the specified amount of tokens and deduct them from the token
			 * bucket. Throws invalid argument exception when the specified amount
			 * exceeds the amount of tokens currently in the bucket.
			 */
			void spend(unsigned int amount);

			/**
			 * Refill the token bucket. The method will check the timer and replenish
			 * the token bucket with overdue tokens. The method will block until
			 * there are tokens to replenish.
			 */
			void refill();

			/**
			 * Refill the token bucket. The method will check the timer and replenish
			 * the token bucket with overdue tokens. The method will not block; it
			 * will instead return an operation which can be executed in pieces via
			 * the `done` and `execute` methods.
			 */
			Refill refill_async();

			/**
			 * Return if the given refill operation is done.
			 */
			bool done(Refill& operation);

			/**
			 * Execute a single part of the refill operation.
			 */
			void execute(Refill& operation);

			/**
			 * Stop the token bucket and the associated timer. Throws timer exception
			 * when the timer cannot be stopped.
			 */
			void stop();

			TokenBucket(const TokenBucket& bucket) = delete;
			TokenBucket(TokenBucket&& bucket) = delete;
			TokenBucket& operator=(const TokenBucket& bucket) = delete;
			TokenBucket& operator=(TokenBucket&& bucket) = delete;

		private:
			int m_timerfd;
			unsigned int m_capacity;
			unsigned int m_fill;
			unsigned int m_period;
			unsigned int m_tokens;
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


