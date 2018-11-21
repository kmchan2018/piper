

#include <cstddef>
#include <cstdint>

#include "exception.hpp"
#include "buffer.hpp"
#include "timestamp.hpp"


#ifndef PREAMBLE_HPP_
#define PREAMBLE_HPP_

namespace Piper
{

	/**
	 * Preamble is the header for each blocks of data in the pipe.
	 */
	struct Preamble
	{
		Timestamp m_timestamp;

		/**
		 * Construct a new preamble.
		 */
		Preamble()
		{
			m_timestamp = now();
		}

		/**
		 * Construct a new preamble from a buffer.
		 */
		Preamble(const Buffer& buffer)
		{
			copy(this, buffer);
		}

		/**
		 * Calculate the latency in nanoseconds, aka the time difference between
		 * block generation and this call.
		 */
		std::int64_t latency() const noexcept
		{
			return now() - m_timestamp;
		}

		/**
		 * Calculate the latency in microseconds, aka the time difference between
		 * block generation and this call.
		 */
		double latency_ms() const noexcept
		{
			return (now() - m_timestamp) / 1000000.0;
		}
	};

}


#endif


