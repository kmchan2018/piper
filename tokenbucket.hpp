

#include <cstddef>
#include <cstdint>

#include "exception.hpp"
#include "timer.hpp"


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
			 * Construct a new token bucket with the given parameters and create the
			 * necessary timer for its operation.
			 */
			TokenBucket(unsigned int capacity, unsigned int fill, unsigned int period);

			/**
			 * Return the timer of the token bucket.
			 */
			const Timer& descriptor() const noexcept { return m_timer; }

			/**
			 * Return the amount of tokens the token bucket can hold.
			 */
			unsigned int capacity() const noexcept { return m_capacity; }

			/**
			 * Return the amount of tokens filled in a single time period.
			 */
			unsigned int fill() const noexcept { return m_fill; }

			/**
			 * Return the number of tokens in the token bucket.
			 */
			unsigned int tokens() const noexcept { return m_tokens; }

			/**
			 * Start the token bucket and the associated timer. Throws timer exception
			 * when the timer cannot be started.
			 */
			void start();

			/**
			 * Stop the token bucket and the associated timer. Throws timer exception
			 * when the timer cannot be stopped.
			 */
			void stop();

			/**
			 * Spend the specified amount of tokens and deduct them from the token
			 * bucket. Throws invalid argument exception when the specified amount
			 * exceeds the amount of tokens currently in the bucket.
			 */
			void spend(unsigned int amount);

			/**
			 * Attempt to refill the token bucket. This method will check the timer
			 * and replenish the token bucket with due tokens. The method will only
			 * return when the bucket is refilled and no longer empty.
			 */
			void refill();

			/**
			 * Attempt to refill the token bucket. This method will return only when:
			 *
			 * 1. The token bucket is refilled; or
			 * 2. The process receives POSIX signal.
			 *
			 * This call is equivalent to calling the `try_refill(int)` method with
			 * timeout -1.
			 */
			void try_refill();

			/**
			 * Attempt to refill the token bucket. This method will return only when:
			 *
			 * 1. The token bucket is refilled; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
			 * Note that the timeout accepts 2 special values. The timeout of 0 means
			 * no waiting. The timeout of -1 indicates that timeout is not observed.
			 */
			void try_refill(int timeout);

			TokenBucket(const TokenBucket& bucket) = delete;
			TokenBucket(TokenBucket&& bucket) = delete;
			TokenBucket& operator=(const TokenBucket& bucket) = delete;
			TokenBucket& operator=(TokenBucket&& bucket) = delete;

		private:
			Timer m_timer;
			unsigned int m_capacity;
			unsigned int m_fill;
			unsigned int m_tokens;
	};

};


#endif


