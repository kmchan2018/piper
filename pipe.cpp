

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "buffer.hpp"
#include "transport.hpp"
#include "pipe.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Helper functions.
	//
	//////////////////////////////////////////////////////////////////////////

	std::size_t calculate_frame_size(Format format, Channel channels)
	{
		ssize_t result = snd_pcm_format_size(format, channels);
		if (result > 0) {
			return result;
		} else {
			throw InvalidArgumentException("invalid format or channels", "pipe.cpp", __LINE__);
		}
	}

	std::size_t calculate_period_size(Format format, Channel channels, Rate rate, Duration period)
	{
		std::size_t frame_size = calculate_frame_size(format, channels);
		std::size_t scaled_period_size = frame_size * rate * period;
		if (scaled_period_size % 1000000000 == 0) {
			return scaled_period_size / 1000000000;
		} else {
			throw InvalidArgumentException("invalid frame size, rate or period", "pipe.cpp", __LINE__);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Metadata implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	inline Pipe::Metadata::Metadata(Format format, Channel channels, Rate rate, Duration period, unsigned int buffer, unsigned int capacity) :
		m_format(format),
		m_channels(channels),
		m_rate(rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(period),
		m_buffer(buffer),
		m_capacity(capacity)
	{
		if (m_format == 0) {
			throw InvalidArgumentException("invalid format", "pipe.cpp", __LINE__);
		} else if (m_channels == 0) {
			throw InvalidArgumentException("invalid channels", "pipe.cpp", __LINE__);
		} else if (m_rate == 0) {
			throw InvalidArgumentException("invalid rate", "pipe.cpp", __LINE__);
		} else if (m_period_time == 0) {
			throw InvalidArgumentException("invalid period", "pipe.cpp", __LINE__);
		} else if (m_buffer <= 1) {
			throw InvalidArgumentException("invalid buffer", "pipe.cpp", __LINE__);
		} else if (m_buffer > UINT32_MAX) {
			throw InvalidArgumentException("invalid buffer", "pipe.cpp", __LINE__);
		} else if (m_capacity <= m_buffer) {
			throw InvalidArgumentException("invalid capacity", "pipe.cpp", __LINE__);
		} else if (m_capacity > UINT32_MAX) {
			throw InvalidArgumentException("invalid capacity", "pipe.cpp", __LINE__);
		} else {
			m_frame_size = calculate_frame_size(m_format, m_channels);
			m_period_size = calculate_period_size(m_format, m_channels, m_rate, m_period_time);
		}
	}

	inline Pipe::Metadata::Metadata(const Metadata& metadata) :
		m_format(metadata.m_format),
		m_channels(metadata.m_channels),
		m_rate(metadata.m_rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(metadata.m_period_time),
		m_buffer(metadata.m_buffer),
		m_capacity(metadata.m_capacity)
	{
		if (m_format == 0) {
			throw InvalidArgumentException("invalid format", "pipe.cpp", __LINE__);
		} else if (m_channels == 0) {
			throw InvalidArgumentException("invalid channels", "pipe.cpp", __LINE__);
		} else if (m_rate == 0) {
			throw InvalidArgumentException("invalid rate", "pipe.cpp", __LINE__);
		} else if (m_period_time == 0) {
			throw InvalidArgumentException("invalid period", "pipe.cpp", __LINE__);
		} else if (m_buffer <= 1) {
			throw InvalidArgumentException("invalid buffer", "pipe.cpp", __LINE__);
		} else if (m_buffer > UINT32_MAX) {
			throw InvalidArgumentException("invalid buffer", "pipe.cpp", __LINE__);
		} else if (m_capacity <= m_buffer) {
			throw InvalidArgumentException("invalid capacity", "pipe.cpp", __LINE__);
		} else if (m_capacity > UINT32_MAX) {
			throw InvalidArgumentException("invalid capacity", "pipe.cpp", __LINE__);
		} else {
			m_frame_size = calculate_frame_size(m_format, m_channels);
			m_period_size = calculate_period_size(m_format, m_channels, m_rate, m_period_time);
		}
	}

	inline Pipe::Metadata& Pipe::Metadata::operator=(const Metadata& metadata)
	{
		if (metadata.m_format == 0) {
			throw InvalidArgumentException("invalid format", "pipe.cpp", __LINE__);
		} else if (metadata.m_channels == 0) {
			throw InvalidArgumentException("invalid channels", "pipe.cpp", __LINE__);
		} else if (metadata.m_rate == 0) {
			throw InvalidArgumentException("invalid rate", "pipe.cpp", __LINE__);
		} else if (metadata.m_period_time == 0) {
			throw InvalidArgumentException("invalid period", "pipe.cpp", __LINE__);
		} else if (metadata.m_buffer <= 1) {
			throw InvalidArgumentException("invalid buffer", "pipe.cpp", __LINE__);
		} else if (metadata.m_capacity <= metadata.m_buffer) {
			throw InvalidArgumentException("invalid capacity", "pipe.cpp", __LINE__);
		} else {
			m_format = metadata.m_format;
			m_channels = metadata.m_channels;
			m_rate = metadata.m_rate;
			m_frame_size = calculate_frame_size(m_format, m_channels);
			m_period_time = metadata.m_period_time;
			m_period_size = calculate_period_size(m_format, m_channels, m_rate, m_period_time);
			m_buffer = metadata.m_buffer;
			m_capacity = metadata.m_capacity;
			return *this;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Pipe implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Pipe::Pipe(const char* path, Format format, Channel channels, Rate rate, Duration period, unsigned int buffer, unsigned int capacity, int mode) :
		m_metadata(format, channels, rate, period, buffer, capacity),
		m_backer(path, Buffer(m_metadata), std::vector<std::size_t>{ sizeof(Preamble), m_metadata.m_period_size }, capacity, mode),
		m_medium(m_backer),
		m_transport(m_medium)
	{
		m_transport.reserve(m_metadata.m_buffer);
	}

	Pipe::Pipe(const char* path) :
		m_backer(path),
		m_medium(m_backer),
		m_transport(m_medium)
	{
		if (sizeof(Metadata) != m_backer.metadata_size()) {
			throw InvalidArgumentException("invalid pipe file", "pipe.cpp", __LINE__);
		} else if (2 != m_backer.component_count()) {
			throw InvalidArgumentException("invalid pipe file", "pipe.cpp", __LINE__);
		} else if (sizeof(Preamble) != m_backer.component_size(0)) {
			throw InvalidArgumentException("invalid pipe file", "pipe.cpp", __LINE__);
		} else {
			const Metadata& temp = m_transport.metadata().to_struct_reference<Metadata>();
		
			if (temp.m_period_size != m_backer.component_size(1)) {
				throw InvalidArgumentException("invalid pipe file", "pipe.cpp", __LINE__);
			} else {
				m_metadata = temp;
				m_transport.reserve(m_metadata.m_buffer);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Outlet implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	void Outlet::watch() const
	{
		watch(-1);
	}

	void Outlet::watch(int timeout) const
	{
		if (timeout == -1) {
			Position current = m_transport.until();
			Duration period = m_pipe.period_time();
			
			while (m_transport.until() == current) {
				Duration limit = period * (m_transport.active() ? 1 : 10);
	
				struct timespec wait;
				wait.tv_sec = limit / 1000000000;
				wait.tv_nsec = limit % 1000000000;
	
				if (::nanosleep(&wait, NULL) < 0) {
					switch (errno) {
						case EINTR: return;
						case EINVAL: throw InvalidArgumentException("invalid argument", "pipe.cpp", __LINE__);
						default: throw SystemException("cannot wait", "pipe.cpp", __LINE__);
					}
				}
			}
		}

		while (timeout > 0) {
			Position current = m_transport.until();
			Duration period = m_pipe.period_time();

			while (m_transport.until() == current) {
				Duration limit = period * (m_transport.active() ? 1 : 10);
				Duration slice = std::min(Duration(timeout), limit * 1000000);
				timeout -= slice;

				struct timespec wait;
				wait.tv_sec = slice / 1000000000;
				wait.tv_nsec = slice % 1000000000;

				if (::nanosleep(&wait, NULL) < 0) {
					switch (errno) {
						case EINTR: return;
						case EINVAL: throw InvalidArgumentException("invalid argument", "pipe.cpp", __LINE__);
						default: throw SystemException("cannot wait", "pipe.cpp", __LINE__);
					}
				}
			}
		}
	}

};


