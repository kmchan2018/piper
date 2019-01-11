

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "buffer.hpp"
#include "transport.hpp"


#ifndef PIPE_HPP_
#define PIPE_HPP_


namespace Piper
{

	/**
	 * Channel indicates the number of channels in a frame. Valid values ranges
	 * from 1 for mono, 2 for stereo and more for surround sound.
	 */
	typedef std::uint32_t Channel;

	/**
	 * Rate indicates the sampling rate in Hz. It is defined as the number of
	 * frames sampled in a single second. Usual values include 44100 for CD
	 * audio, 48000 for DVD audio, etc.
	 */
	typedef std::uint32_t Rate;

	/**
	 * Preamble stores information about a block in the pipe. It primarily
	 * contains the timestamp of the block.
	 */
	struct Preamble
	{
		Timestamp timestamp;

		explicit Preamble() : timestamp(now()) {}
		explicit Preamble(Timestamp timestamp) : timestamp(timestamp) {}
	};

	/**
	 * Pipe is a specialization of channel.
	 */
	class Pipe
	{
		public:

			/**
			 * Create a new pipe with the given parameters. The method will
			 * throw exception when it cannot create the file.
			 */
			explicit Pipe(const char* path, const char* format, Channel channels, Rate rate, Duration period, unsigned int readable, unsigned int writable, unsigned int separation, int mode); 

			/**
			 * Open an existing pipe. The method will throw exception when it
			 * cannot open the file. Additionally, the method can fail if the
			 * file is not fully initialized yet.
			 */
			explicit Pipe(const char* path);

			/**
			 * Return the path of the pipe.
			 */
			const std::string& path() const noexcept { return m_backer.path(); }

			/**
			 * Return the transport of the pipe.
			 */
			const Transport& transport() const noexcept { return m_transport; }

			/**
			 * Return the format name of the pipe.
			 */
			const char* format_name() const noexcept { return m_metadata.m_format; }

			/**
			 * Return the ALSA format code of the pipe.
			 */
			snd_pcm_format_t format_code_alsa() const noexcept;

			/**
			 * Return the channel of the pipe.
			 */
			Channel channels() const noexcept { return m_metadata.m_channels; }

			/**
			 * Return the sampling rate of the pipe.
			 */
			Rate rate() const noexcept { return m_metadata.m_rate; }

			/**
			 * Return the frame size of the pipe.
			 */
			std::size_t frame_size() const noexcept { return m_metadata.m_frame_size; }

			/**
			 * Return the period of the pipe in term of time.
			 */
			Duration period_time() const noexcept { return m_metadata.m_period_time; }

			/**
			 * Return the period of the pipe in term of size.
			 */
			std::size_t period_size() const noexcept { return m_metadata.m_period_size; }

			/**
			 * Return the read window of the pipe in term of period.
			 */
			unsigned int readable() const noexcept { return m_metadata.m_readable; };

			/**
			 * Return the read window of the pipe in term of time.
			 */
			Duration readable_time() const noexcept { return m_metadata.m_period_time * m_metadata.m_readable; }

			/**
			 * Return the read window of the pipe in term of size.
			 */
			std::size_t readable_size() const noexcept { return m_metadata.m_period_size * m_metadata.m_readable; }

			/**
			 * Return the write window of the pipe in term of period.
			 */
			unsigned int writable() const noexcept { return m_metadata.m_writable; };

			/**
			 * Return the write window of the pipe in term of time.
			 */
			Duration writable_time() const noexcept { return m_metadata.m_period_time * m_metadata.m_writable; }

			/**
			 * Return the write window of the pipe in term of size.
			 */
			std::size_t writable_size() const noexcept { return m_metadata.m_period_size * m_metadata.m_writable; }

			/**
			 * Return the capacity of the pipe in term of period.
			 */
			unsigned int capacity() const noexcept { return m_backer.slot_count(); }

			/**
			 * Return the buffer of the pipe in term of time.
			 */
			Duration capacity_time() const noexcept { return m_metadata.m_period_time * m_backer.slot_count(); }

			/**
			 * Return the buffer of the pipe in term of size.
			 */
			std::size_t capacity_size() const noexcept { return m_metadata.m_period_size * m_backer.slot_count(); }

			Pipe(const Pipe& pipe) = delete;
			Pipe(Pipe&& pipe) = delete;
			Pipe& operator=(const Pipe& pipe) = delete;
			Pipe& operator=(Pipe&& pipe) = delete;

			friend class Inlet;
			friend class Outlet;

		private:

			/**
			 * Pipe metadata version code specifies the version of the metadata
			 * header.
			 */
			static const std::uint32_t VERSION = 1;

			/**
			 * Format length is a constant representing the maximum length of format
			 * strings including the terminating NULL. It also represents the space
			 * reserved for the format field in the metadata structure.
			 */
			static const unsigned int MAX_FORMAT_SIZE = 28;

			/**
			 * Metadata stores information about a pipe, including acceptable audio
			 * format, buffer size, etc.
			 */
			struct Metadata
			{
				std::uint32_t m_version = VERSION;
				char m_format[MAX_FORMAT_SIZE];
				Channel m_channels;
				Rate m_rate;
				std::uint32_t m_frame_size;
				std::uint32_t m_period_size;
				Duration m_period_time;
				std::uint32_t m_readable;
				std::uint32_t m_writable;

				explicit Metadata() = default;
				explicit Metadata(const char* format, Channel channels, Rate rate, Duration period, unsigned int readable, unsigned int writable);
				explicit Metadata(const Metadata& metadata);
				Metadata& operator=(const Metadata& metadata);
			};

			static_assert(offsetof(Metadata, m_format) - offsetof(Metadata, m_version) == sizeof(Metadata::m_version), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_channels) - offsetof(Metadata, m_format) == sizeof(Metadata::m_format), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_rate) - offsetof(Metadata, m_channels) == sizeof(Metadata::m_channels), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_frame_size) - offsetof(Metadata, m_rate) == sizeof(Metadata::m_rate), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_period_size) - offsetof(Metadata, m_frame_size) == sizeof(Metadata::m_frame_size), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_period_time) - offsetof(Metadata, m_period_size) == sizeof(Metadata::m_period_size), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_readable) - offsetof(Metadata, m_period_time) == sizeof(Metadata::m_period_time), "incorrect layout for pipe metadata");
			static_assert(offsetof(Metadata, m_writable) - offsetof(Metadata, m_readable) == sizeof(Metadata::m_readable), "incorrect layout for pipe metadata");
			static_assert(sizeof(Metadata) - offsetof(Metadata, m_writable) == sizeof(Metadata::m_writable), "incorrect layout for pipe metadata");

			Metadata m_metadata;
			Backer m_backer;
			Medium m_medium;
			Transport m_transport;
	};

	/**
	 * Inlet implements a class that writes data to a pipe.
	 */
	class Inlet
	{
		public:

			typedef Transport::Position Position;
			typedef Transport::Session Session;

			/**
			 * Create a new inlet into the given pipe. Throws concurrent session
			 * exception when another outlet is created for the pipe.
			 */
			explicit Inlet(Pipe* pipe) : Inlet(*pipe) {}

			/**
			 * Create a new inlet into the given pipe. Throws concurrent session
			 * exception when another outlet is created for the pipe.
			 */
			explicit Inlet(Pipe& pipe) : m_pipe(pipe), m_transport(pipe.m_transport), m_session(m_transport.begin()) {}

			/**
			 * Destroy this inlet.
			 */
			~Inlet()
			{
				m_transport.finish(m_session);
			}

			/**
			 * Return the pipe.
			 */
			const Pipe& pipe() const noexcept { return m_pipe; }

			/**
			 * Return the pipe.
			 */
			Pipe& pipe() noexcept { return m_pipe; }

			/**
			 * Return the session.
			 */
			Session session() noexcept { return m_session; }

			/**
			 * Return the write window.
			 */
			Position window() const noexcept { return m_transport.writable(); }

			/**
			 * Return the position for the first staging block.
			 */
			Position start() const noexcept { return m_transport.middle(); }

			/**
			 * Return the position for the last staging block.
			 */
			Position until() const noexcept { return m_transport.until(); }

			/**
			 * Return the preamble of the given staging block for update.
			 */
			Preamble& preamble(Position position) { return m_transport.input(m_session, position, 0).to_struct_reference<Preamble>(); }

			/**
			 * Return the content of the given staging block for update.
			 */
			Buffer content(Position position) { return m_transport.input(m_session, position, 1); }

			/**
			 * Flush the first staging block. The staging block will be converted to
			 * a cousmable block, and a new staging block will be appended to the
			 * end of the pipe.
			 */
	    void flush() { return m_transport.flush(m_session); }

			Inlet(const Inlet& inlet) = delete;
			Inlet(Inlet&& inlet) = delete;
			Inlet& operator=(const Inlet& inlet) = delete;
			Inlet& operator=(Inlet&& inlet) = delete;

		private:

			Pipe& m_pipe;
			Transport& m_transport;
			Session m_session;
			
	};

	/**
	 * Outlet implements a class that reads data from a pipe.
	 */
	class Outlet
	{
		public:

			typedef Transport::Position Position;

			/**
			 * Create a new outlet from the given pipe.
			 */
			explicit Outlet(Pipe* pipe) : Outlet(*pipe) {}

			/**
			 * Create a new outlet from the given pipe.
			 */
			explicit Outlet(Pipe& pipe) : m_pipe(pipe), m_transport(m_pipe.m_transport) {}

			/**
			 * Return the pipe.
			 */
			const Pipe& pipe() const noexcept { return m_pipe; }

			/**
			 * Return the pipe.
			 */
			Pipe& pipe() noexcept { return m_pipe; }

			/**
			 * Return the read window.
			 */
			Position window() const noexcept { return m_transport.readable(); }

			/**
			 * Return the position for the first readable block. Note that the
			 * return value of this method may equal to that of the until method,
			 * which indicates that the pipe is empty and there are no readable
			 * block.
			 */
			Position start() const noexcept { return m_transport.start(); }

			/**
			 * Return the position for the first writable block, one block past the
			 * last readable block. Note that the return value of this method may
			 * equal to that of the until method, which indicates that the pipe is
			 * empty and there are no readable block.
			 */
			Position until() const noexcept { return m_transport.middle(); }

			/**
			 * Return the preamble of the given staging block for update.
			 */
			const Preamble& preamble(Position position) const { return m_transport.view(position, 0).to_struct_reference<Preamble>(); }

			/**
			 * Return the content of the given staging block for update.
			 */
			const Buffer content(Position position) const { return m_transport.view(position, 1); }

			/**
			 * Watch for next writes to the pipe. The method will return when the
			 * pipe is written with new blocks, or when the calling process receives
			 * incoming signal.
			 */
			void watch() const;

			/**
			 * Watch for next writes to the pipe. The method will return when the
			 * pipe is written with new blocks, or when the calling process receives
			 * incoming signal, or when the specified timeout has elapsed.
			 */
			void watch(int timeout) const;

			Outlet(const Outlet& outlet) = delete;
			Outlet(Outlet&& outlet) = delete;
			Outlet& operator=(const Outlet& outlet) = delete;
			Outlet& operator=(Outlet&& outlet) = delete;

		private:

			Pipe& m_pipe;
			Transport& m_transport;

	};

};


#endif


