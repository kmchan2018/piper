

#include <cstdint>

#include <time.h>


#ifndef TIMESTAMP_HPP_
#define TIMESTAMP_HPP_


namespace Piper
{

	/**
	 * Timestamp is a simple struct that represents a specific point of time.
	 * The value is specified as the duration from a system-local epoch.
	 */
	typedef std::int64_t Timestamp;

	/**
	 * Return the current timestamp.
	 */
	Timestamp now()
	{
		struct timespec time;
		::clock_gettime(CLOCK_MONOTONIC, &time);
		return Timestamp{ time.tv_sec * 1000000000L + time.tv_nsec };
	}

};


#endif


