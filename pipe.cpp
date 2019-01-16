

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "buffer.hpp"
#include "transport.hpp"
#include "pipe.hpp"


#define EXC_SYSTEM(err) std::system_error(err, std::system_category(), strerror(err))
#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "pipe.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "pipe.cpp", __LINE__);


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Helper functions.
	//
	//////////////////////////////////////////////////////////////////////////

	static inline std::size_t calculate_frame_size(snd_pcm_format_t format, Channel channels)
	{
		ssize_t result = snd_pcm_format_size(format, channels);
		if (result > 0) {
			return static_cast<std::size_t>(result);
		} else {
			EXC_START(std::invalid_argument("[Piper::calculate_frame_size] Cannot calculate frame size due to invalid format and/or channels"));
		}
	}

	static inline std::size_t calculate_period_size(snd_pcm_format_t format, Channel channels, Rate rate, Duration period)
	{
		std::size_t frame_size = calculate_frame_size(format, channels);
		std::size_t scaled_period_size = frame_size * rate * period;
		if (scaled_period_size % 1000000000 == 0) {
			return scaled_period_size / 1000000000;
		} else {
			EXC_START(std::invalid_argument("[Piper::calculate_period_size] Cannot calculate period size due to invalid rate and/or duration"));
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Metadata implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	inline Piper::Pipe::Metadata::Metadata(const char* format, Channel channels, Rate rate, Duration period, unsigned int readable, unsigned int writable) :
		m_channels(channels),
		m_rate(rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(period),
		m_readable(readable),
		m_writable(writable)
	{
		std::size_t size = std::strlen(format);
		snd_pcm_format_t code = snd_pcm_format_value(format);

		if (size >= MAX_FORMAT_SIZE) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (code == SND_PCM_FORMAT_UNKNOWN) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (m_channels == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid channels"));
		} else if (m_rate == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid rate"));
		} else if (m_period_time == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid period"));
		} else if (m_readable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid readable"));
		} else if (m_readable > UINT32_MAX) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid readable"));
		} else if (m_writable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid writable"));
		} else if (m_writable > UINT32_MAX) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid writable"));
		} else {
			std::memset(m_format, 0, MAX_FORMAT_SIZE);
			std::memcpy(m_format, format, size);

			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
		}
	}

	inline Piper::Pipe::Metadata::Metadata(const Metadata& metadata) :
		m_channels(metadata.m_channels),
		m_rate(metadata.m_rate),
		m_frame_size(0),
		m_period_size(0),
		m_period_time(metadata.m_period_time),
		m_readable(metadata.m_readable),
		m_writable(metadata.m_writable)
	{
		snd_pcm_format_t code = snd_pcm_format_value(metadata.m_format);

		if (metadata.m_version != VERSION) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid version"));
		} else if (metadata.m_format[MAX_FORMAT_SIZE - 1] != 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (code == SND_PCM_FORMAT_UNKNOWN) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (m_channels == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid channels"));
		} else if (m_rate == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid rate"));
		} else if (m_period_time == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid period"));
		} else if (m_readable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid readable"));
		} else if (m_writable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid writable"));
		} else {
			std::memset(m_format, 0, MAX_FORMAT_SIZE);
			std::memcpy(m_format, metadata.m_format, MAX_FORMAT_SIZE - 1);

			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
		}
	}

	inline Piper::Pipe::Metadata& Piper::Pipe::Metadata::operator=(const Metadata& metadata)
	{
		snd_pcm_format_t code = snd_pcm_format_value(metadata.m_format);

		if (metadata.m_version != VERSION) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid version"));
		} else if (metadata.m_format[MAX_FORMAT_SIZE - 1] != 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (code == SND_PCM_FORMAT_UNKNOWN) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid format"));
		} else if (metadata.m_channels == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid channels"));
		} else if (metadata.m_rate == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid rate"));
		} else if (metadata.m_period_time == 0) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid period"));
		} else if (metadata.m_readable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid readable"));
		} else if (metadata.m_writable <= 1) {
			EXC_START(std::invalid_argument("[Piper::Pipe::Metadata::Metadata] Cannot create metadata due to invalid writable"));
		} else {
			std::memset(m_format, 0, MAX_FORMAT_SIZE);
			std::memcpy(m_format, metadata.m_format, MAX_FORMAT_SIZE - 1);

			m_channels = metadata.m_channels;
			m_rate = metadata.m_rate;
			m_frame_size = calculate_frame_size(code, m_channels);
			m_period_time = metadata.m_period_time;
			m_period_size = calculate_period_size(code, m_channels, m_rate, m_period_time);
			m_readable = metadata.m_readable;
			m_writable = metadata.m_writable;

			return *this;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Pipe implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Pipe::Pipe(const char* path, const char* format, Channel channels, Rate rate, Duration period, unsigned int readable, unsigned int writable, unsigned int separation, unsigned int mode) :
		m_metadata(format, channels, rate, period, readable, writable),
		m_backer(path, Buffer(m_metadata), std::vector<std::size_t>{ sizeof(Preamble), m_metadata.m_period_size }, readable + writable + separation, mode),
		m_medium(m_backer),
		m_transport(m_medium)
	{
		try {
			m_transport.set_writable(m_metadata.m_writable);
			m_transport.set_readable(m_metadata.m_readable);
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Pipe::Pipe] Cannot create pipe file due to invalid argument to underlying component"));
		}
	}

	Pipe::Pipe(const char* path) :
		m_backer(path),
		m_medium(m_backer),
		m_transport(m_medium)
	{
		if (sizeof(Metadata) != m_backer.metadata_size()) {
			EXC_START(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
		} else if (2 != m_backer.component_count()) {
			EXC_START(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
		} else if (sizeof(Preamble) != m_backer.component_size(0)) {
			EXC_START(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
		}

		try {
			const Metadata& temp = m_transport.metadata().to_struct_reference<Metadata>();

			if (temp.m_readable + temp.m_writable > m_backer.slot_count()) {
				EXC_START(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
			} else if (temp.m_period_size != m_backer.component_size(1)) {
				EXC_START(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
			} else {
				m_metadata = temp;
			}
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(PipeCorruptedException("[Piper::Pipe::Pipe] Cannot open pipe file due to file corruption"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Pipe::Pipe] Cannot open pipe file due to logic error in underlying component"));
		}

		try {
			m_transport.set_writable(m_metadata.m_writable);
			m_transport.set_readable(m_metadata.m_readable);
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Pipe::Pipe] Cannot open pipe file due to borked transport sanity check"));
		}
	}

	snd_pcm_format_t Pipe::format_code_alsa() const noexcept
	{
		return snd_pcm_format_value(m_metadata.m_format);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Inlet implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Inlet::Inlet(Pipe& pipe) :
		m_pipe(pipe),
		m_transport(pipe.m_transport),
		m_session(0)
	{
		try {
			m_session = m_transport.begin();
		} catch (TransportConcurrentSessionException& ex) {
			EXC_CHAIN(PipeConcurrentInletException("[Piper::Inlet::Inlet] Cannot create another inlet for the pipe due to existing inlet"));
		}
	}

	Preamble& Inlet::preamble(Position position)
	{
		try {
			return m_transport.input(m_session, position, 0).to_struct_reference<Preamble>();
		} catch (std::invalid_argument& ex) {
			// NB: there is also a possible that the issue is due to corrupted
			// m_session, but this is usually not the case.
			EXC_CHAIN(std::invalid_argument("[Piper::Inlet::preamble] Cannot return block preamble due to invalid position"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Inlet::preamble] Cannot return block preamble due to logic error in underlying component"));
		}
	}

	Buffer Inlet::content(Position position)
	{
		try {
			return m_transport.input(m_session, position, 1);
		} catch (std::invalid_argument& ex) {
			// NB: there is also a possible that the issue is due to corrupted
			// m_session, but this is usually not the case.
			EXC_CHAIN(std::invalid_argument("[Piper::Inlet::content] Cannot return block content due to invalid position"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Inlet::content] Cannot return block preamble due to logic error in underlying component"));
		}
	}

	void Inlet::flush()
	{
		try {
			m_transport.flush(m_session);
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Inlet::flush] Cannot flush the inlet due to corrupted session"));
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Outlet implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	const Preamble& Outlet::preamble(Position position) const
	{
		try {
			return m_transport.view(position, 0).to_struct_reference<Preamble>();
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(std::invalid_argument("[Piper::Outlet::preamble] Cannot return block preamble due to invalid position"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Outlet::preamble] Cannot return block preamble due to logic error in underlying component"));
		}
	}

	const Buffer Outlet::content(Position position) const
	{
		try {
			return m_transport.view(position, 1);
		} catch (std::invalid_argument& ex) {
			EXC_CHAIN(std::invalid_argument("[Piper::Inlet::content] Cannot return block content due to invalid position"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::Inlet::content] Cannot return block preamble due to logic error in underlying component"));
		}
	}

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
				wait.tv_sec = static_cast<time_t>(limit / 1000000000);
				wait.tv_nsec = static_cast<long>(limit % 1000000000);
	
				if (::nanosleep(&wait, nullptr) < 0) {
					switch (errno) {
						case EINTR: return;
						case EINVAL: EXC_START(std::logic_error("[Piper::Outlet::watch] Cannot watch for incoming blocks due to unexpected sleep error"));
						default: EXC_START(EXC_SYSTEM(errno), PipeWatchException("[Piper::Outlet::watch] Cannot watch for incoming blocks due to operating system error"));
					}
				}
			}
		}

		while (timeout > 0) {
			Position current = m_transport.until();
			Duration period = m_pipe.period_time();

			while (m_transport.until() == current) {
				int limit = (period / 1000000) * (m_transport.active() ? 1 : 10);
				int slice = std::min(timeout, limit);
				timeout -= slice;

				struct timespec wait;
				wait.tv_sec = static_cast<time_t>(slice / 1000);
				wait.tv_nsec = static_cast<long>((slice % 1000) * 1000000);

				if (::nanosleep(&wait, nullptr) < 0) {
					switch (errno) {
						case EINTR: return;
						case EINVAL: EXC_START(std::logic_error("[Piper::Outlet::watch] Cannot watch for incoming blocks due to unexpected sleep error"));
						default: EXC_START(EXC_SYSTEM(errno), PipeWatchException("[Piper::Outlet::watch] Cannot watch for incoming blocks due to operating system error"));
					}
				}
			}
		}
	}

}


