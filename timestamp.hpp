

#include <cstdint>

#include <time.h>


#ifndef TIMESTAMP_HPP_
#define TIMESTAMP_HPP_


namespace Piper
{

	/**
	 * Timestamp is a 64-bit integer that represents a specific point of time.
	 * The value is specified as the nanosecond duration from a system-local
	 * epoch.
	 */
	typedef std::int64_t Timestamp;

	/**
	 * Duration represents a time period in nanoseconds. The type is always
	 * 64-bit wide.
	 */
	typedef std::uint64_t Duration;

	/**
	 * Return the current timestamp.
	 */
	inline Timestamp now()
	{
		struct timespec time;
		::clock_gettime(CLOCK_MONOTONIC, &time);
		return Timestamp{ time.tv_sec * 1000000000L + time.tv_nsec };
	}

}


#endif


