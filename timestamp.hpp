

#include <cstddef>
#include <cstdint>

#include <time.h>


#ifndef TIMESTAMP_H_
#define TIMESTAMP_H_


namespace Piper
{

	/**
	 * Timestamp is a simple struct that represents a specific point of time.
	 * The value is specified as the duration from a system-local epoch.
	 */
	struct Timestamp
	{
		std::uint64_t value;
	};

	/**
	 * Return the current timestamp.
	 */
	Timestamp now()
	{
		struct timespec time;
		clock_gettime(CLOCK_MONOTONIC, &time);
		return Timestamp{ time.tv_sec * 1000000000L + time.tv_nsec };
	}

	/**
	 * Compute the difference between the two timestamps.
	 */
	std::int64_t operator-(Timestamp& t1, Timestamp& t2)
	{
		return t1.value - t2.value;
	}

};


#endif


