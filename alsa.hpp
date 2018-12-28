

#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "errno.h"
#include <poll.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/pcm_ioplug.h>


#ifndef ALSA_HPP_
#define ALSA_HPP_


namespace ALSA
{

	/**
	 * Range implements a mutable pointer to a section of audio buffer that can be
	 * worked on.
	 */
	class Range
	{
		public:

			/**
			 * Construct a new range with the given layout.
			 */
			Range(snd_pcm_format_t format, unsigned int channels);

			/**
			 * Check if the range is valid.
			 */
			bool valid() const noexcept;

			/**
			 * Reset the cursor to the given buffer. Throws exception if the given
			 * buffer cannot match the audio buffer layout expected by the range.
			 */
			void reset(char* buffer, std::size_t size);

			/**
			 * Reset the cursor to the given area.
			 */
			void reset(const snd_pcm_channel_area_t* areas, snd_pcm_uframes_t length);

			/**
		 	 * Update the range to remove the first few frames. Throws exception
			 * when the number of frames to remove exceeds the number of frames
			 * covered by the range.
			 */
			void behead(snd_pcm_uframes_t frames);

			/**
			 * Copy data from the source range to the target range and return the
			 * amount of frames copied. Both source and target are not beheaded.
			 */
			static snd_pcm_uframes_t copy(Range& target, Range& source, snd_pcm_uframes_t maximum);

			/**
			 * Copy data from the source range to the target range and return the
			 * amount of frames copied. Both source and target are beheaded by the
			 * amount copied.
			 */
			static snd_pcm_uframes_t copy_behead(Range& target, Range& source, snd_pcm_uframes_t maximum);

		private:

			snd_pcm_format_t m_format;
			unsigned int m_channels;
			unsigned int m_unit;
			std::vector<snd_pcm_channel_area_t> m_areas;
			snd_pcm_uframes_t m_offset;
			snd_pcm_uframes_t m_length;

	};

	/**
	 * Handle to the IOPlug implementation.
	 */
	class IOPlug
	{
		public:

			/**
			 * IOPlug core data. This is the central piece of data for the IOPlug
			 * implementation. The actual IOPlug struct points its private data to
			 * this structure, and from here callbacks can access other parts of
			 * the implementation.
			 */
			struct Data
			{
				snd_pcm_ioplug_t ioplug;
				snd_pcm_ioplug_callback_t callback;
				snd_pcm_uframes_t boundary;
				IOPlug* handle;
				bool trace = false;
			};

			/**
			 * IOPlug options. The options is used to configure the PCM device
			 * before its creation.
			 */
			struct Options
			{
				const char* name = nullptr;
				bool mmap = false;
				bool listed = false;
				bool monotonic = false;
				int poll_fd = -1;
				int poll_events = 0;
				bool enable_hw_params_callback = false;
				bool enable_hw_free_callback = false;
				bool enable_prepare_callback = false;
				bool enable_drain_callback = false;
				bool enable_pause_callback = false;
				bool enable_resume_callback = false;
				bool enable_poll_descriptors_count_callback = false;
				bool enable_poll_descriptors_callback = false;
				bool enable_poll_revents_callback = false;
				bool enable_transfer_callback = false;
				bool enable_dump_callback = false;
				bool enable_delay_callback = false;
			};

			/**
			 * Abstract class for IOPlug callback handler. The IOPlug system uses
			 * callbacks to trigger PCM device actions.
			 */
			class Handler
			{
				public:

					/**
					 * Virtual destructor.
					 */
					virtual ~Handler() {}

					/**
					 * The method is called to setup the PCM device before its creation.
					 */
					virtual void configure(const char* name, snd_pcm_stream_t stream, int mode, Options& options) {}

					/**
					 * The method is called to setup the PCM device after its creation.
					 */
					virtual void create(IOPlug& ioplug) {}

					/**
					 * The method is called to apply new hardware parameters onto the PCM
					 * device.
					 */
					virtual void hw_params(IOPlug& ioplug, snd_pcm_hw_params_t *params) {}

					/**
					 * The method is called to clear existing hardware parameters on the PCM
					 * device.
					 */
					virtual void hw_free(IOPlug& ioplug) {}

					/**
					 * The method is called to apply new software parameters onto the PCM
					 * device.
					 */
					virtual void sw_params(IOPlug& ioplug, snd_pcm_sw_params_t *params) {}

					/**
					 * The method is called to prepare the PCM device for playback/capture.
					 */
					virtual void prepare(IOPlug& ioplug) {}

					/**
					 * The method is called to start playback/capture on the PCM device.
					 */
					virtual void start(IOPlug& ioplug) = 0;

					/**
					 * The method is called to stop playback/capture on the PCM device.
					 */
					virtual void stop(IOPlug& ioplug) = 0;

					/**
					 * The method is called to finish off remaining audio data in the PCM
					 * device buffer before playback/capture is over.
					 */
					virtual void drain(IOPlug& ioplug) {}

					/**
					 * The method is called to pause current playback/capture on the PCM
					 * device.
					 */
					virtual void pause(IOPlug& ioplug, int enable) {}

					/**
					 * The method is called to resume the PCM device from suspension.
					 */
					virtual void resume(IOPlug& ioplug) {}

					/**
					 * The method is called to report the number of file descriptors
					 * application should poll for updates.
					 */
					virtual int poll_descriptors_count(IOPlug& ioplug) { return 0; }

					/**
					 * The method is called to report the list of file descriptors
					 * application should poll for updates.
					 */
					virtual int poll_descriptors(IOPlug& ioplug, struct pollfd *pfd, unsigned int space) { return 0; }

					/**
					 * The method is called to process poll results and return the
					 * actual event that happened: POLLIN indicates that the PCM device
					 * can be read; POLLOUT indicates that the PCM device can be written.
					 */
					virtual void poll_revents(IOPlug& ioplug, struct pollfd *pfd, unsigned int nfds, unsigned short *revents) { *revents = 0; }

					/**
					 * The method is called to query current hardware buffer position of
					 * the PCM device.
					 */
					virtual snd_pcm_uframes_t pointer(IOPlug& ioplug) = 0;

					/**
					 * The method is called to transfer audio data from/into the PCM device
					 * buffer.
					 */
					virtual snd_pcm_uframes_t transfer(IOPlug& ioplug, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size) { return 0; }

					/**
					 * The method is called to dump PCM device details to the output.
					 */
					virtual void dump(IOPlug& ioplug, snd_output_t* out) {}

					/**
					 * The method is called to report the PCM device latency, aka the
					 * time between audio sample is written and it is played on the
					 * physical hardware.
					 */
					virtual void delay(IOPlug& ioplug, snd_pcm_sframes_t *delayp) {}

					/**
					 * The method is called to release any resources used by the PCM device
					 * on close.
					 */
					virtual void close(IOPlug& ioplug) {}

			};

			/**
			 * Return the stream direction of the PCM device.
			 */
			snd_pcm_stream_t stream() const noexcept { return m_data->ioplug.stream; }

			/**
			 * Return the stream direction of the PCM device.
			 */
			snd_pcm_state_t state() noexcept { return m_data->ioplug.state; }

			/**
			 * Return the current access mode of the PCM device. Note that it is only
			 * valid after the device is configured.
			 */
			snd_pcm_access_t access() const noexcept { return m_data->ioplug.access; }

			/**
			 * Return the current format of the PCM device. Note that it is only valid
			 * after the device is configured.
			 */
			snd_pcm_format_t format() noexcept { return m_data->ioplug.format; }

			/**
			 * Return the current channels of the PCM device. Note that it is only
			 * valid after the device is configured.
			 */
			unsigned int channels() noexcept { return m_data->ioplug.channels; }

			/**
			 * Return the current sampling rate of the PCM device. Note that it is
			 * only valid after the device is configured.
			 */
			unsigned int rate() noexcept { return m_data->ioplug.rate; }

			/**
			 * Return the current period size (in frames) of the PCM device. Note that
			 * it is only valid after the device is configured.
			 */
			snd_pcm_uframes_t period_size() noexcept { return m_data->ioplug.period_size; }

			/**
			 * Return the current buffer size (in frames) of the PCM device. Note that
			 * it is only valid after the device is configured.
			 */
			snd_pcm_uframes_t buffer_size() noexcept { return m_data->ioplug.buffer_size; }

			/**
			 * Return the current boundary of the PCM device (in frames). The boundary
			 * indicates the limits of hardware and application pointers. Wrap-around
			 * will occur when any pointer exceeds the boundary.
			 */
			snd_pcm_uframes_t boundary() noexcept { return m_data->boundary; }

			/**
			 * Return the current hardware pointer of the PCM device.
			 */
			snd_pcm_uframes_t hardware_pointer() noexcept { return m_data->ioplug.hw_ptr; }

			/**
			 * Return the current application pointer of the PCM device.
			 */
			snd_pcm_uframes_t application_pointer() noexcept { return m_data->ioplug.appl_ptr; }

			/**
			 * Return the used space (in frames) in the PCM device buffer.
			 */
			snd_pcm_uframes_t buffer_used() noexcept;

			/**
			 * Return the free space (in frames) in the PCM device buffer.
			 */
			snd_pcm_uframes_t buffer_free() noexcept;

			/**
			 * Return the mapped memory region of the PCM device. It is only
			 * available when mmap flag is configured. Returns null when not
			 * available.
			 */
			const snd_pcm_channel_area_t* mmap_area() noexcept { return snd_pcm_ioplug_mmap_areas(&m_data->ioplug); }

			/**
			 * Return the callback handler of the PCM device.
			 */
			Handler* handler() noexcept { return m_handler.get(); }

			/**
			 * Move the PCM device to the given state.
			 */
			void set_state(snd_pcm_state_t state);

			/**
			 * Restrict the given hardware parameter to the given range.
			 */
			void set_parameter_range(int type, unsigned int min, unsigned int max);

			/**
			 * Restrict the given hardware parameter to the given list.
			 */
			void set_parameter_list(int type, unsigned int len, unsigned int* list);

			/**
			 * Clear all restrictions on hardware parameters.
			 */
			void reset_parameters() noexcept { snd_pcm_ioplug_params_reset(&m_data->ioplug); }

			/**
			 * Convert a PCM device pointer to its buffer position.
			 */
			snd_pcm_uframes_t calculate_buffer_index(snd_pcm_uframes_t pointer) noexcept { return pointer % m_data->ioplug.buffer_size; }

			/**
			 * Create a new PCM device.
			 */
			static snd_pcm_t* open(const char* name, snd_pcm_stream_t stream, int mode, std::unique_ptr<Handler>&& handler);

		private:

			/**
			 * Construct a new IOPlug object.
			 */
			IOPlug(std::unique_ptr<Data>&& data, std::unique_ptr<Handler>&& handler) : m_data(std::move(data)), m_handler(std::move(handler)) {}

			std::unique_ptr<Data> m_data;
			std::unique_ptr<Handler> m_handler;

	};

	/**
	 * Exception thrown when ALSA subsystem encounters some error.
	 */
	class BaseException : public std::system_error
	{
		public:
			BaseException(const char* message, int errcode) : std::system_error(errcode, std::system_category(), message) {}
			virtual ~BaseException() {}
			int errcode() const noexcept { return code().value(); }
	};

	/**
	 * Exception thrown when device cannot be read from or written to.
	 */
	class IOException : public BaseException
	{
		public:
			IOException() : BaseException("device IO error", -EIO) {}
			virtual ~IOException() {}
	};

	/**
	 * Exception thrown when device buffer has overrunned or underrunned.
	 */
	class XrunException : public BaseException
	{
		public:
			XrunException() : BaseException("device buffer xrun", -EPIPE) {}
			virtual ~XrunException() {}
	};

	/**
	 * Exception thrown when device is suspended.
	 */
	class SuspendedException : public BaseException
	{
		public:
			SuspendedException() : BaseException("device suspended", -ESTRPIPE) {}
			virtual ~SuspendedException() {}
	};

	/**
	 * Exception thrown when device is corrupted.
	 */
	class CorruptedException : public BaseException
	{
		public:
			CorruptedException() : BaseException("device corrupted", -EBADF) {}
			virtual ~CorruptedException() {}
	};

	/**
	 * Exception thrown when device is disconnected.
	 */
	class DisconnectedException : public BaseException
	{
		public:
			DisconnectedException() : BaseException("device disconnected", -ENOTTY) {}
			virtual ~DisconnectedException() {}
	};

};


#endif


