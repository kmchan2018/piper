

#include <exception>
#include <stdexcept>

#include <alsa/asoundlib.h>

#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"


#ifndef DEVICE_HPP_
#define DEVICE_HPP_


namespace Piper
{

	/**
	 * This class defines the abstract base class for playback devices. The use
	 * case of a playback device instance is as follow:
	 *
	 * First of all, the device has to be configured for a pipe so that the
	 * device can accept audio data from the pipe. Depending on each device,
	 * it is possible to configure the number of period the device will buffer
	 * before the playback actually starts, and allows the device to tolerate
	 * some minmor jitter to the audio stream timing.
	 *
	 * Next, the start method can be called. After the call, the device will
	 * accept writes. The actual playback will start once the buffer is filled
	 * to the configured prebuffer level.
	 *
	 * Finally, the stop method can be called to stop the playback. Whether the
	 * device will process remaining buffered data is implementation defined.
	 *
	 * Notes on Partial Frames
	 * =======================
	 *
	 * The most fundamental unit in audio transmission is frame which is usually
	 * larger than a byte. It creates a problem where partial frames may be
	 * written. Currently all implemented devices can deal with partial frames.
	 *
	 */
	class PlaybackDevice
	{
		public:

			/**
			 * Configure the playback device for the pipe.
			 */
			virtual void configure(const Pipe& pipe, unsigned int prebuffer) = 0;

			/**
			 * Start the playback. The call will configure the device so that it
			 * can be written to and playback will start once enough data is
			 * accumulated.
			 */
			virtual void start() = 0;

			/**
			 * Stop the playback.
			 */
			virtual void stop() = 0;

			/**
			 * Write audio data to the playback device. The method is blocking and
			 * return only until all data in the buffer is written, signals
			 * notwithstanding.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given buffer should have its length equal to
			 * multiples of frame size, or the method will throw exception.
			 */
			virtual void write(const Buffer buffer) = 0;

			/**
			 * Write audio data to the playback device. Unlike the write variant,
			 * the method will wait until the device is ready, write some data to
			 * the device and then return. Signals may interrupt the method and
			 * cause it to return early without doing any writes.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given source should have its remainder equal to
			 * multiples of frame size, or the method will throw exception.
			 */
			virtual void try_write(Source& source) = 0;

			/**
			 * Write audio data to the playback device. Unlike the write variant,
			 * the method will wait until the device is ready or until the timeout
			 * has elapsed, write some data to the device and then return. Signals
			 * may interrupt the method and cause it to return early without doing
			 * any writes.
			 *
			 * The timeout is measured in microseconds. Two special sentinel values
			 * are also allowed. 0 indicates zero waiting, making the method totally
			 * non-blocking. -1 means indefinite waiting, causing the method to
			 * behave like the other try_write variant without timeout parameter.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given source should have its remainder equal to
			 * multiples of frame size, or the method will throw exception.
			 */
			virtual void try_write(Source& source, int timeout) = 0;

	};

	/**
	 * This class defines the abstract base class for capture devices. The use
	 * case of a playback device instance is as follow:
	 *
	 * First of all, the device has to be configured for a pipe so that the
	 * device can read audio data acceptable to the pipe. Unlike playback
	 * devices, capture devices do not do any buffering.
	 *
	 * Next, the start method can be called. After the call, the device will
	 * be read from.
	 *
	 * Finally, the stop method can be called to stop the capture. Audio data
	 * remaining in the device buffer will be discarded.
	 *
	 * Notes on Partial Frames
	 * =======================
	 *
	 * The most fundamental unit in audio transmission is frame which is usually
	 * larger than a byte. It creates a problem where partial frames may be
	 * read. Currently all implemented devices can deal with partial frames.
	 *
	 */
	class CaptureDevice
	{
		public:

			/**
			 * Configure the capture device for the pipe.
			 */
			virtual void configure(const Pipe& pipe) = 0;

			/**
			 * Start the capture.
			 */
			virtual void start() = 0;

			/**
			 * Stop the capture.
			 */
			virtual void stop() = 0;

			/**
			 * Read audio data from the capture device. The method is blocking and
			 * return only until the buffer is completely filled with data, signals
			 * notwithstanding.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given buffer should have its length equal to
			 * multiples of frame size, or the method will throw exception.
			 */
			virtual void read(Buffer buffer) = 0;

			/**
			 * Read audio data from the capture device. Unlike the read variant,
			 * the method will wait until the device is ready, read some data from
			 * the device and then return. Signals may interrupt the method and
			 * cause it to return early without doing any reads.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given destination should have both its remainder
			 * equal to multiples of frame size, or the method will throw exception.
			 */
			virtual void try_read(Destination& destination) = 0;

			/**
			 * Read audio data from the capture device. Unlike the read variant,
			 * the method will wait until the device is ready or until the timeout
			 * has elapsed, read some data from the device and then return. Signals
			 * may interrupt the method and cause it to return early without doing
			 * any reads.
			 *
			 * The timeout is measured in microseconds. Two special sentinel values
			 * are also allowed. 0 indicates zero waiting, making the method totally
			 * non-blocking. -1 means indefinite waiting, causing the method to
			 * behave like the other try_write variant without timeout parameter.
			 *
			 * The method MAY or MAY NOT support partial frames. When partial frame
			 * is not supported, the given destination should have both its remainder
			 * equal to multiples of frame size, or the method will throw exception.
			 */
			virtual void try_read(Destination& destination, int timeout) = 0;

	};

	/**
	 * This class implements a playback device that sends audio data to standard
	 * output. The class supports partial frames for all write member functions.
	 */
	class StdoutPlaybackDevice : public PlaybackDevice
	{
		public:

			/**
			 * Construct a new stdout playback device.
			 */
			StdoutPlaybackDevice();

			/**
			 * Do nothing.
			 */
			void configure(const Pipe& pipe, unsigned int prebuffer) override {}

			/**
			 * Do nothing.
			 */
			void start() override {}

			/**
			 * Do nothing.
			 */
			void stop() override {}

			/**
			 * Write audio data to the standard output.
			 */
			void write(const Buffer buffer) override;

			/**
			 * Write audio data to the standard output.
			 */
			void try_write(Source& source) override;

			/**
			 * Write audio data to the standard output.
			 */
			void try_write(Source& source, int timeout) override;

			StdoutPlaybackDevice(const StdoutPlaybackDevice& device) = delete;
			StdoutPlaybackDevice(StdoutPlaybackDevice&& device) = delete;
			StdoutPlaybackDevice& operator=(const StdoutPlaybackDevice& device) = delete;
			StdoutPlaybackDevice& operator=(StdoutPlaybackDevice&& device) = delete;

		private:

			/**
			 * File handle for standard output.
			 */
			File m_file;

	};

	/**
	 * This class implements a capture device that reads audio data from standard
	 * input. The class supports partial frames for all read member functions.
	 */
	class StdinCaptureDevice : public CaptureDevice
	{
		public:

			/**
			 * Construct a new stdin capture device.
			 */
			StdinCaptureDevice();

			/**
			 * Do nothing.
			 */
			void configure(const Pipe& pipe) override {}

			/**
			 * Do nothing.
			 */
			void start() override {}

			/**
			 * Do nothing.
			 */
			void stop() override {}

			/**
			 * Read audio data from standard input.
			 */
			void read(Buffer buffer) override;

			/**
			 * Read audio data from standard input.
			 */
			void try_read(Destination& destination) override;

			/**
			 * Read audio data from standard input.
			 */
			void try_read(Destination& destination, int timeout) override;

			StdinCaptureDevice(const StdinCaptureDevice& device) = delete;
			StdinCaptureDevice(StdinCaptureDevice&& device) = delete;
			StdinCaptureDevice& operator=(const StdinCaptureDevice& device) = delete;
			StdinCaptureDevice& operator=(StdinCaptureDevice&& device) = delete;

		private:

			/**
			 * File handle for standard input.
			 */
			File m_file;

	};

	/**
	 * This class implements a playback device that sends audio data to ALSA
	 * PCM device. The class supports partial frames for all write member
	 * functions.
	 */
	class AlsaPlaybackDevice : public PlaybackDevice
	{
		public:

			/**
			 * Construct a new alsa playback device.
			 */
			AlsaPlaybackDevice(const char* name);

			/**
			 * Destruct the alsa playback device.
			 */
			~AlsaPlaybackDevice();

			/**
			 * Configure the playback device for the pipe. It will configure the ALSA
			 * PCM according to the pipe and calculate the frame size for unit
			 * conversion.
			 */
			void configure(const Pipe& pipe, unsigned int prebuffer) override;

			/**
			 * Prepare the ALSA PCM so that its buffer can be written into. Playback
			 * will start automatically once enough audio data is accumulated.
			 */
			void start() override;

			/**
			 * Stop the playback in the ALSA PCM and drop all remaining audio data in
			 * the buffer.
			 */
			void stop() override;

			/**
			 * Write audio data to the ALSA PCM buffer.
			 */
			void write(const Buffer buffer) override;

			/**
			 * Write audio data to the ALSA PCM buffer.
			 */
			void try_write(Source& source) override;

			/**
			 * Write audio data to the ALSA PCM buffer.
			 */
			void try_write(Source& source, int timeout) override;

			AlsaPlaybackDevice(const AlsaPlaybackDevice& device) = delete;
			AlsaPlaybackDevice(AlsaPlaybackDevice&& device) = delete;
			AlsaPlaybackDevice& operator=(const AlsaPlaybackDevice& device) = delete;
			AlsaPlaybackDevice& operator=(AlsaPlaybackDevice&& device) = delete;

		private:

			/**
			 * Handle of the ALSA PCM device.
			 */
			snd_pcm_t* m_handle;

			/**
			 * Frame size of the audio data accepted by the device. It is used for
			 * size conversion in write member functions. It also determines the
			 * size of the partial frame cache.
			 */
			std::size_t m_frame_size;

			/**
			 * Size of the unwritten partial frame fragment cached in the device.
			 * Zero indicates there are no unwritten partial frame. Positive value
			 * indicates that the cache contains unwritten fragment of that length
			 * in the beginning of the cache.
			 */
			std::size_t m_partial_size;

			/**
			 * Pointer to the partial frame cache. Its size is specified by the
			 * `m_frame_size` member variable. Unwritten data can be found at the
			 * beginning of the cache, and its amount is specified by the
			 * `m_partial_size` member variable.
			 */
			char* m_partial_data;

	};

	/**
	 * This class implements a capture device that reads audio data from ALSA
	 * PCM device. The class supports partial frames for all read member
	 * functions.
	 */
	class AlsaCaptureDevice : public CaptureDevice
	{
		public:

			/**
			 * Construct a new stdin capture device.
			 */
			AlsaCaptureDevice(const char* name);

			/**
			 * Destruct the alsa capture device.
			 */
			~AlsaCaptureDevice();

			/**
			 * Do nothing.
			 */
			void configure(const Pipe& pipe) override;

			/**
			 * Do nothing.
			 */
			void start() override;

			/**
			 * Do nothing.
			 */
			void stop() override;

			/**
			 * Read audio data from standard input.
			 */
			void read(Buffer buffer) override;

			/**
			 * Read audio data from standard input.
			 */
			void try_read(Destination& destination) override;

			/**
			 * Read audio data from standard input.
			 */
			void try_read(Destination& destination, int timeout) override;

			AlsaCaptureDevice(const AlsaCaptureDevice& device) = delete;
			AlsaCaptureDevice(AlsaCaptureDevice&& device) = delete;
			AlsaCaptureDevice& operator=(const AlsaCaptureDevice& device) = delete;
			AlsaCaptureDevice& operator=(AlsaCaptureDevice&& device) = delete;

		private:

			/**
			 * Handle of the ALSA PCM device.
			 */
			snd_pcm_t* m_handle;

			/**
			 * Frame size of the audio data returned by the device. It is used for
			 * size conversion in read member functions. It also determines the
			 * size of the partial frame cache.
			 */
			std::size_t m_frame_size;

			/**
			 * Size of the unread partial frame fragment cached in the device. Zero
			 * indicates there are no unread partial frame. Positive value indicates
			 * that the cache contains unread fragment of that length in the end of
			 * the cache.
			 */
			std::size_t m_partial_size;

			/**
			 * Pointer to the partial frame cache. Its size is specified by the
			 * `m_frame_size` member variable. Unread data can be found at the end
			 * of the cache, and its amount is specified by the `m_partial_size`
			 * member variable.
			 */
			char* m_partial_data;

	};

	/**
	 * Exception thrown the device cannot be operation upon due to some
	 * permanent, unrecoverable reason like device removal.
	 */
	class DeviceException : public std::runtime_error
	{
		public:
			using std::runtime_error::runtime_error;
	};

	/**
	 * Exception thrown the device cannot be worked with due to some permanent,
	 * unrecoverable reason like device corruption, removal, etc.
	 */
	class DeviceUnusableException : public DeviceException
	{
		public:
			using DeviceException::DeviceException;
	};

	/**
	 * Exception thrown the device fails to start or continue playback for some
	 * temporary reason like buffer underrun. The device should be able to
	 * recover from the error by restarting the playback.
	 */
	class DevicePlaybackException : public DeviceException
	{
		public:
			using DeviceException::DeviceException;
	};

	/**
	 * Exception thrown the device fails to start or continue capture for some
	 * temporary reason like buffer overrun. The device should be able to
	 * recover from the error by restarting the capture.
	 */
	class DeviceCaptureException : public DeviceException
	{
		public:
			using DeviceException::DeviceException;
	};

};


#endif


