

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
	 * This class implements a ALSA IOPlug PCM device. This class wraps around
	 * the IOPlug subsystem of the ALSA library and provides a C++ interface to
	 * that functionality.
	 *
	 * To create a new device, the constructor can be used. It takes a few
	 * parameters, in particular the implementation instance that backs the
	 * device. The device will take ownership of the implementation instance
	 * and manages its lifecycle as indicated by the unique pointer parameter
	 * type. It is therefore generally incorrect to share an implementation
	 * instance among multiple devices.
	 *
	 * Upon creation, this class instance will own the underlying device handle;
	 * destruction of the instance will destroy the underlying handle as well.
	 * The release member function will cause this class instance to give up
	 * the ownership and return a pointer to the snd_pcm_t structure in the
	 * underlying device handle. This design is to ensure proper cleanup and
	 * disposal of underlying handle in spite of exceptions and other scenario
	 * where a device is created but left unused.
	 */
	class IOPlug
	{
		public:

			class Control;
			class Implementation;

			/**
			 * IOPlug handle. This is the central piece of data for a IOPlug device.
			 * The snd_pcm_ioplug_t structure points its private data to this handle,
			 * and from here callbacks can access other data they will ever need.
			 *
			 * The `ioplug` field contains the actual snd_pcm_ioplug_t structure.
			 * The `callback` field contains the callback table. These two fields
			 * are the data structure from IOPlug subsystem of the ALSA library.
			 *
			 * The `boundary` field stores the boundary software parameter of the
			 * device. It is the wrap-around point of both hardware and application
			 * pointers and therefore pretty much essential in correct operation
			 * of the device. Curiously, ALSA snd_pcm_ioplug_t DO NOT expose this
			 * parameter for implementation. Hence, this field is added to track
			 * it.
			 *
			 * The `control` field points to a control object that can query, control
			 * and tune the the IOPlug device. The `implementation` fields points to
			 * the implementation of the device.
			 *
			 * Finally, some of the fields points to some auxillary data about the
			 * the device. The `name` field contains the device name; the `trace`
			 * field indicates if callback tracing is enabled.
			 *
			 * This structure owns both the control and implementation instance once
			 * assigned.
			 */
			struct Handle
			{
				snd_pcm_ioplug_t ioplug;
				snd_pcm_ioplug_callback_t callback;
				snd_pcm_uframes_t boundary;
				std::unique_ptr<Control> control;
				std::unique_ptr<Implementation> implementation;
				std::string name;
				bool trace = false;
			};

			/**
			 * IOPlug options. The options is used to configure the PCM device
			 * before its creation.
			 *
			 * The `name` field points to a C-style string carrying the name of the
			 * PCM device.
			 *
			 * The `mmap` field indicates if a memory buffer should be allocated by
			 * ALSA automatically for mmap style transfer. If this is set to true,
			 * the `mmap_area` member function of the hndle class will return the
			 * backing memory buffer. Otherwise, the member function will return
			 * nullptr.
			 *
			 * The `poll_fd` and `poll_events` fields contain the descriptor and
			 * events the device can be polled.
			 *
			 * The `enable_xxx_callback` fields indicates if a particular callback
			 * is implemented. Setting the corresponding field to true will cause
			 * the corresponding member function of the implementation class to be
			 * called.
			 */
			struct Options
			{
				const char* name = nullptr;
				bool mmap = false;
				bool listed = false;
				bool monotonic = false;
				int poll_fd = -1;
				unsigned int poll_events = 0;
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
			 * Control to the IOPlug device. This class allows implementation classes
			 * to query, control and tune the controlled device. Many callbacks in
			 * the implementation class will be given a reference of this class so
			 * that they can work with the device.
			 */
			class Control
			{
				public:

					/**
					 * Construct a new control.
					 */
					Control(Handle& handle) : m_handle(handle) {}

					/**
					 * Return the stream direction of the PCM device.
					 */
					snd_pcm_stream_t stream() const noexcept { return m_handle.ioplug.stream; }

					/**
					 * Return the current state of the PCM device.
					 */
					snd_pcm_state_t state() noexcept { return m_handle.ioplug.state; }

					/**
					 * Return the current access mode of the PCM device. Note that it is
					 * only valid after the device is configured.
					 */
					snd_pcm_access_t access() const noexcept { return m_handle.ioplug.access; }

					/**
					 * Return the current format of the PCM device. Note that it is only
					 * valid after the device is configured.
					 */
					snd_pcm_format_t format() noexcept { return m_handle.ioplug.format; }

					/**
					 * Return the current channels of the PCM device. Note that it is
					 * only valid after the device is configured.
					 */
					unsigned int channels() noexcept { return m_handle.ioplug.channels; }

					/**
					 * Return the current sampling rate of the PCM device. Note that it
					 * is only valid after the device is configured.
					 */
					unsigned int rate() noexcept { return m_handle.ioplug.rate; }

					/**
					 * Return the current period size (in frames) of the PCM device. Note
					 * that it is only valid after the device confirms its hardware
					 * parameters.
					 */
					snd_pcm_uframes_t period_size() noexcept { return m_handle.ioplug.period_size; }

					/**
					 * Return the current buffer size (in frames) of the PCM device. Note
					 * that it is only valid after the device confirms its hardware
					 * parameters.
					 */
					snd_pcm_uframes_t buffer_size() noexcept { return m_handle.ioplug.buffer_size; }

					/**
					 * Return the current boundary of the PCM device (in frames). It
					 * indicates the upper limits of hardware and application pointers.
					 * Wrap-around will occur when any pointer exceeds the boundary.
					 * Note that it is only valid after the device confirms its software
					 * parameters.
					 */
					snd_pcm_uframes_t boundary() noexcept { return m_handle.boundary; }

					/**
					 * Return the current hardware pointer of the PCM device. Note that
					 * it is only valid after the device is prepared.
					 */
					snd_pcm_uframes_t hardware_pointer() noexcept { return m_handle.ioplug.hw_ptr; }

					/**
					 * Return the current application pointer of the PCM device. Note that
					 * it is only valid after the device is prepared.
					 */
					snd_pcm_uframes_t application_pointer() noexcept { return m_handle.ioplug.appl_ptr; }

					/**
					 * Return the used space (in frames) in the PCM device buffer. Note
					 * that it is only valid after the device is prepared.
					 */
					snd_pcm_uframes_t buffer_used() noexcept;

					/**
					 * Return the free space (in frames) in the PCM device buffer. Note
					 * that it is only valid after the device is prepared.
					 */
					snd_pcm_uframes_t buffer_free() noexcept;

					/**
					 * Return the mapped memory region of the PCM device. It is only
					 * available when mmap flag is configured. Returns null when not
					 * available.
					 */
					const snd_pcm_channel_area_t* mmap_area() noexcept { return snd_pcm_ioplug_mmap_areas(&m_handle.ioplug); }

					/**
					 * Move the PCM device to the given state.
					 */
					void set_state(snd_pcm_state_t state);

					/**
					 * Restrict the given hardware parameter to the given range. While
					 * it can be called any time, it is only effective before the device
					 * negotiates its hardware parameters.
					 */
					void set_parameter_range(int type, unsigned int min, unsigned int max);

					/**
					 * Restrict the given hardware parameter to the given list. While it
					 * can be called any time, it is only effective before the device
					 * negotiates its hardware parameters.
					 */
					void set_parameter_list(int type, unsigned int len, unsigned int* list);

					/**
					 * Clear all restrictions on hardware parameters. While it can be
					 * called any time, it is only effective before the device negotiates
					 * its hardware parameters.
					 */
					void reset_parameters() noexcept { snd_pcm_ioplug_params_reset(&m_handle.ioplug); }

					/**
					 * Convert a PCM device pointer to its buffer position. Note that
					 * it is only valid after the device confirms its hardware
					 * parameters.
					 */
					snd_pcm_uframes_t calculate_buffer_index(snd_pcm_uframes_t pointer) noexcept { return pointer % m_handle.ioplug.buffer_size; }

					/**
					 * Calculate the updated hardware pointer after the given increment.
					 * The member function will throws invalid argument exception if the
					 * increment is larger than the device buffer size. Note that it is
					 * only valid after the device confirms its hardware parameters.
					 */
					snd_pcm_uframes_t calculate_next_hardware_pointer(snd_pcm_uframes_t increment);

					Control(const Control& control) = delete;
					Control(Control&& control) = delete;
					Control& operator=(const Control& control) = delete;
					Control& operator=(Control&& control) = delete;

				private:

					Handle& m_handle;

			};

			/**
			 * Abstract base class for IOPlug implementation. When user applications
			 * performs some operations on an IOPlug PCM device, callbacks from the
			 * implementation class will be invoked to complete the operation.
			 */
			class Implementation
			{
				public:

					/**
					 * Empty virtual destructor to ensure proper destruction of derived
					 * classes. 
					 */
					virtual ~Implementation() {}

					/**
					 * The method is called to setup the PCM device before its creation.
					 * Both `name`, `stream` and `mode` parameters are passed from the
					 * device constructor. The callback should update the `options`
					 * parameter to setup the device.
					 */
					virtual void configure([[ gnu::unused ]] const char* name, [[ gnu::unused ]] snd_pcm_stream_t stream, [[ gnu::unused ]] int mode, [[ gnu::unused ]] Options& options) {}

					/**
					 * The method is called to setup the PCM device after its creation.
					 * This is the opportunity to configure the restrictions on hardware
					 * parameters.
					 */
					virtual void create([[ gnu::unused ]] Control& control) {}

					/**
					 * The method is called to apply new hardware parameters onto the PCM
					 * device.
					 */
					virtual void hw_params([[ gnu::unused ]] Control& control, [[ gnu::unused ]] snd_pcm_hw_params_t *params) {}

					/**
					 * The method is called to clear existing hardware parameters on the PCM
					 * device.
					 */
					virtual void hw_free([[ gnu::unused ]] Control& control) {}

					/**
					 * The method is called to apply new software parameters onto the PCM
					 * device.
					 */
					virtual void sw_params([[ gnu::unused ]] Control& control, [[ gnu::unused ]] snd_pcm_sw_params_t *params) {}

					/**
					 * The method is called to prepare the PCM device for playback/capture.
					 */
					virtual void prepare([[ gnu::unused ]] Control& control) {}

					/**
					 * The method is called to start playback/capture on the PCM device.
					 */
					virtual void start(Control& control) = 0;

					/**
					 * The method is called to stop playback/capture on the PCM device.
					 */
					virtual void stop(Control& control) = 0;

					/**
					 * The method is called to finish off remaining audio data in the PCM
					 * device buffer before playback/capture is over.
					 */
					virtual void drain([[ gnu::unused ]] Control& control) {}

					/**
					 * The method is called to pause current playback/capture on the PCM
					 * device.
					 */
					virtual void pause([[ gnu::unused ]] Control& control, [[ gnu::unused ]] int enable) {}

					/**
					 * The method is called to resume the PCM device from suspension.
					 */
					virtual void resume([[ gnu::unused ]] Control& control) {}

					/**
					 * The method is called to report the number of file descriptors
					 * application should poll for updates.
					 */
					virtual int poll_descriptors_count([[ gnu::unused ]] Control& control) { return 0; }

					/**
					 * The method is called to report the list of file descriptors
					 * application should poll for updates.
					 */
					virtual int poll_descriptors([[ gnu::unused ]] Control& control, [[ gnu::unused ]] struct pollfd *pfd, [[ gnu::unused ]] unsigned int space) { return 0; }

					/**
					 * The method is called to process poll results and return the
					 * actual event that happened: POLLIN indicates that the PCM device
					 * can be read; POLLOUT indicates that the PCM device can be written.
					 */
					virtual void poll_revents([[ gnu::unused ]] Control& control, [[ gnu::unused ]] struct pollfd *pfd, [[ gnu::unused ]] unsigned int nfds, unsigned short *revents) { *revents = 0; }

					/**
					 * The method is called to query current hardware buffer position of
					 * the PCM device.
					 */
					virtual snd_pcm_uframes_t pointer(Control& control) = 0;

					/**
					 * The method is called to transfer audio data from/into the PCM device
					 * buffer.
					 */
					virtual snd_pcm_uframes_t transfer([[ gnu::unused ]] Control& control, [[ gnu::unused ]] const snd_pcm_channel_area_t *areas, [[ gnu::unused ]] snd_pcm_uframes_t offset, [[ gnu::unused ]] snd_pcm_uframes_t size) { return 0; }

					/**
					 * The method is called to dump PCM device details to the output.
					 */
					virtual void dump([[ gnu::unused ]] Control& control, [[ gnu::unused ]] snd_output_t* out) {}

					/**
					 * The method is called to report the PCM device latency, aka the
					 * time between audio sample is written and it is played on the
					 * physical hardware.
					 */
					virtual void delay([[ gnu::unused ]] Control& control, [[ gnu::unused ]] snd_pcm_sframes_t *delayp) {}

					/**
					 * The method is called to release any resources used by the PCM device
					 * on close.
					 */
					virtual void close([[ gnu::unused ]] Control& control) {}

			};

			/**
			 * Construct a new IOPlug device.
			 */
			IOPlug(const char* name, snd_pcm_stream_t stream, int mode, std::unique_ptr<Implementation>&& implementation);

			/**
			 * Destruct this IOPlug device. The underlying device handle will be
			 * destroyed if this instance still owns it. Otherwise, the destructor
			 * does nothing.
			 */
			~IOPlug();

			/**
			 * Get the implementation of the IOPlug device. This member function
			 * will work only before `release` is invoked on the instance. After
			 * the `release` call, this function will throw exception.
			 */
			Implementation& implementation() const;

			/**
			 * Hand over the ALSA snd_pcm_t pointer to the caller. After the call
			 * this IOPlug instance will no long own the underlying device handle
			 * and its use and disposal will be up to the caller.
			 */
			snd_pcm_t* release() noexcept;

		private:

			/**
			 * Pointer to the core IOPlug data. It should point to a valid device
			 * handle after creation, but reset to nullptr after call to `release`
			 * member function.
			 */
			Handle* m_handle;

	};

	/**
	 * Returns an exception representing some common error.
	 */
	inline std::system_error error(int errcode, const char* message) noexcept
	{
		return std::system_error(errcode, std::system_category(), message);
	}

	/**
	 * Returns an exception representing input/output error.
	 */
	inline std::system_error io_error() noexcept
	{
		return std::system_error(-EIO, std::system_category(), "device IO error");
	}

	/**
	 * Returns an exception representing buffer xrun.
	 */
	inline std::system_error xrun_error() noexcept
	{
		return std::system_error(-EPIPE, std::system_category(), "device buffer xrun");
	}

	/**
	 * Returns an exception representing device suspension.
	 */
	inline std::system_error suspended_error() noexcept
	{
		return std::system_error(-ESTRPIPE, std::system_category(), "device suspended");
	}

	/**
	 * Returns an exception representing device disconnection.
	 */
	inline std::system_error disconnected_error() noexcept
	{
		return std::system_error(-ENODEV, std::system_category(), "device disconnected");
	}

	/**
	 * Returns an exception representing device corruption.
	 */
	inline std::system_error corrupted_error() noexcept
	{
		return std::system_error(-EBADF, std::system_category(), "device corrupted");
	}

}


#endif


