

#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"


#ifndef DEVICE_HPP_
#define DEVICE_HPP_


namespace Piper
{

	/**
	 * This class defines the abstract base class for playback devices.
	 */
	class PlaybackDevice
	{
		public:

			/**
			 * Configure the playback device for the pipe.
			 */
			virtual void configure(const Pipe& pipe, unsigned int preroll)= 0;

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
			 * Write audio data to the playback device.
			 */
			virtual void write(const Buffer buffer) = 0;

			/**
			 * Write audio data to the playback device.
			 */
			virtual void try_write(Source& source) = 0;

			/**
			 * Write audio data to the playback device.
			 */
			virtual void try_write(Source& source, int timeout) = 0;

	};

	/**
	 * This class defines the abstract base class for capture devices.
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
			 * Read audio data from the capture device.
			 */
			virtual void read(Buffer buffer) = 0;

			/**
			 * Read audio data from the capture device.
			 */
			virtual void try_read(Destination& destination) = 0;

			/**
			 * Read audio data from the capture device.
			 */
			virtual void try_read(Destination& destination, int timeout) = 0;

	};

	/**
	 * This class implements a playback device that sends audio data to standard
	 * output.
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
			void configure(const Pipe& pipe, unsigned int preroll) override {}

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

			File m_file;

	};

	/**
	 * This class implements a capture device that reads audio data from standard
	 * input.
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

			File m_file;

	};

	/**
	 * Exception thrown the device cannot be operation upon due to some
	 * permanent, unrecoverable reason like device removal.
	 */
	class DeviceException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception thrown the device fails to start or continue playback for some
	 * temporary reason like buffer underrun. The device should be able to
	 * recover from the error by restarting the playback.
	 */
	class PlaybackException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception thrown the device fails to start or continue capture for some
	 * temporary reason like buffer overrun. The device should be able to
	 * recover from the error by restarting the capture.
	 */
	class CaptureException : public Exception
	{
		public:
			using Exception::Exception;
	};

};


#endif


