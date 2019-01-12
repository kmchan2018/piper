

#include <exception>
#include <stdexcept>

#include "timestamp.hpp"
#include "timer.hpp"


#ifndef TOKENBUCKET_HPP_
#define TOKENBUCKET_HPP_


namespace Piper
{

	/**
	 * A token bucket is an algorithm for flow control and traffic shaping.
	 * 
	 * Overview
	 * ========
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
	 * Implementation Details
	 * ======================
	 *
	 * This implementation is backed by the timer class found in timer.hpp and
	 * timer.cpp, by turning accumulated ticks into tokens in the bucket.
	 *
	 * Issues
	 * ======
	 *
	 * Due to its use of the timer class, this class is also restricted to
	 * Linux only.
	 *
	 */
	class TokenBucket
	{
		public:

			/**
			 * Construct a new token bucket with the given parameters and create the
			 * necessary timer for its operation.
			 */
			TokenBucket(unsigned int capacity, unsigned int fill, Duration period);

			/**
			 * Return the timer of the token bucket.
			 */
			const Timer& timer() const noexcept { return m_timer; }

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

			/**
			 * Timer backing the token bucket. The period of this bucket equals to
			 * the period of the backing timer.
			 */
			Timer m_timer;

			/**
			 * Capacity of the token bucket aka the maximum number of tokens allowed
			 * in the bucket.
			 */
			unsigned int m_capacity;

			/**
			 * Number of tokens to be replenished for each elapsed period.
			 */
			unsigned int m_fill;

			/**
			 * The current number of tokens in the bucket. It is bounded between 0
			 * and capacity.
			 */
			unsigned int m_tokens;
	};

	/**
	 * Exception thrown when operation on a token bucket failed due to some
	 * run time error including any errors of the underlying timer.
	 */
	class TokenBucketException : public std::runtime_error
	{
		public:
			using std::runtime_error::runtime_error;
	};

};


#endif


