

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>
#ifdef USE_PULSE
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

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

	std::size_t calculate_frame_size(snd_pcm_format_t format, Channel channels)
	{
		ssize_t result = snd_pcm_format_size(format, channels);
		if (result > 0) {
			return result;
		} else {
			throw InvalidArgumentException("invalid format or channels", "pipe.cpp", __LINE__);
		}
	}

	std::size_t calculate_period_size(snd_pcm_format_t format, Channel channels, Rate rate, Duration period)
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

	inline Pipe::Metadata::Metadata(const char* format, Channel channels, Rate rate, Duration period, unsigned int buffer, unsigned int capacity) :
		m_channels(channels),
		m_rate(rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(period),
		m_buffer(buffer),
		m_capacity(capacity)
	{
		snd_pcm_format_t code = snd_pcm_format_value(format);

		if (code == SND_PCM_FORMAT_UNKNOWN) {
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
			std::memset(m_format, 0, sizeof(m_format));
			std::memcpy(m_format, format, sizeof(m_format) - 1);

			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
		}
	}

	inline Pipe::Metadata::Metadata(const Metadata& metadata) :
		m_channels(metadata.m_channels),
		m_rate(metadata.m_rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(metadata.m_period_time),
		m_buffer(metadata.m_buffer),
		m_capacity(metadata.m_capacity)
	{
		snd_pcm_format_t code = snd_pcm_format_value(metadata.m_format);

		if (code == SND_PCM_FORMAT_UNKNOWN) {
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
			std::memset(m_format, 0, sizeof(m_format));
			std::memcpy(m_format, metadata.m_format, sizeof(m_format) - 1);

			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
		}
	}

	inline Pipe::Metadata& Pipe::Metadata::operator=(const Metadata& metadata)
	{
		snd_pcm_format_t code = snd_pcm_format_value(metadata.m_format);

		if (code == SND_PCM_FORMAT_UNKNOWN) {
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
			std::memset(m_format, 0, sizeof(m_format));
			std::memcpy(m_format, metadata.m_format, sizeof(m_format) - 1);

			m_channels = metadata.m_channels;
			m_rate = metadata.m_rate;
			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_time = metadata.m_period_time;
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
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

	Pipe::Pipe(const char* path, const char* format, Channel channels, Rate rate, Duration period, unsigned int buffer, unsigned int capacity, int mode) :
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

	snd_pcm_format_t Pipe::format_code_alsa() const noexcept
	{
		return snd_pcm_format_value(m_metadata.m_format);
	}

#ifdef USE_PULSE
	pa_sample_format_t Pipe::format_code_pulse() const
	{
		switch (snd_pcm_format_value(m_metadata.m_format)) {
			case SND_PCM_FORMAT_U8:       return PA_SAMPLE_U8;
			case SND_PCM_FORMAT_S16_LE:   return PA_SAMPLE_S16LE;
			case SND_PCM_FORMAT_S16_BE:   return PA_SAMPLE_S16BE;
			case SND_PCM_FORMAT_FLOAT_LE: return PA_SAMPLE_FLOAT32LE;
			case SND_PCM_FORMAT_FLOAT_BE: return PA_SAMPLE_FLOAT32BE;
			case SND_PCM_FORMAT_A_LAW:    return PA_SAMPLE_ALAW;
			case SND_PCM_FORMAT_MU_LAW:   return PA_SAMPLE_ULAW;
			case SND_PCM_FORMAT_S32_LE:   return PA_SAMPLE_S32LE;
			case SND_PCM_FORMAT_S32_BE:   return PA_SAMPLE_S32BE;
			case SND_PCM_FORMAT_S24_3LE:  return PA_SAMPLE_S24LE;
			case SND_PCM_FORMAT_S24_3BE:  return PA_SAMPLE_S24BE;
			case SND_PCM_FORMAT_S24_LE:   return PA_SAMPLE_S24_32LE;
			case SND_PCM_FORMAT_S24_BE:   return PA_SAMPLE_S24_32BE;
			default: throw UnsupportedFormatException("unsupported format", "pipe.cpp", __LINE__);
		}
	}
#endif

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


