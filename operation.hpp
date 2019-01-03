

#include "exception.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "device.hpp"


#ifndef OPERATION_HPP_
#define OPERATION_HPP_


namespace Piper
{

	/**
	 * Callback defines functions that will be invoked when specific events
	 * occured during an operation.
	 */
	class Callback
	{
		public:

			/**
			 * Called when a feed operation begins.
			 */
			virtual void on_begin_feed(const Pipe& pipe, const CaptureDevice& device) {}

			/**
			 * Called when a drain operation begins.
			 */
			virtual void on_begin_drain(const Pipe& pipe, const PlaybackDevice& device) {}

			/**
			 * Called whenever a transfer from/into the pipe occured.
			 */
			virtual void on_transfer(const Preamble& preamble, const Buffer& buffer) {}

			/**
			 * Called whenever an operation reaches a point where its execution
			 * can be interrupted.
			 */
			virtual void on_tick() {};

			/**
			 * Called when the current operation ends.
			 */
			virtual void on_end() {}

			/**
			 * Destruct the callback instance.
			 */
			virtual ~Callback() {}

	};

	/**
	 * Feed operation feeds data from a capture device to a pipe.
	 *
	 * Implementation Details
	 * ======================
	 *
	 * The feed operation consists of two phases, preparation and transfer
	 * phases.
	 *
	 * To start the preparation, the first thing to do is to prepare the pipe
	 * for writing. It involves creating an inlet to the pipe and saving its
	 * write position.
	 *
	 * The next step in the preparation is to create a token bucket to control
	 * the data flow rate. It prevents the operation from transferring data too
	 * fast.
	 *
	 * The next step in the preparation is then to configure the capture device
	 * for the pipe. It ensure that data from the capture device to be compatible
	 * to the pipe.
	 *
	 * Finally, to finish the preparation and start the transfer, both the token
	 * bucket and the capture device will be started.
	 *
	 * The transfer phase runs in a loop and each loop will perform an action
	 * depending on whether the token bucket is empty. If the token bucket is
	 * empty, the operation should wait until the bucket is refilled. Otherwise,
	 * a single period of audio data is copied from the capture device to the
	 * inlet, the current write position is advanced and a token is deducted from
	 * the token bucket.
	 *
	 * The transfer phase can be terminated when any exception is thrown. When
	 * the transfer finishes, cleanup is done by stopping the capture device
	 * and the token bucket.
	 *
	 * Throughout the operation, appropriate callback will be invoked to report
	 * the progress. They can be used to stop the operation by throwing
	 * exceptions.
	 *
	 */
	class FeedOperation
	{
		public:

			/**
			 * Construct a new feed operation.
			 */
			explicit FeedOperation(Callback& callback) : m_callback(callback) {}

			/**
			 * Get the callback used in the feed operation.
			 */
			const Callback& callback() const noexcept { return m_callback; }

			/**
			 * Execute the feed operation. Note that any exception thrown by the
			 * callback will stop the operation and it will then be rethrown
			 * verbatim.
			 */
			void execute(Pipe& pipe, CaptureDevice& device);

		private:

			/**
			 * Callback invoked during the operation.
			 */
			Callback& m_callback;

	};

	/**
	 * Drain operation drains data from a pipe device to a playback device.
	 *
	 * Implementation Details
	 * ======================
	 *
	 * The drain operation consists of two phases, preparation and transfer
	 * phases.
	 *
	 * To start the preparation, the first thing to do is to prepare the pipe
	 * for reading. It involves creating an outlet to the pipe and saving its
	 * read position.
	 *
	 * The next step in the preparation is to create a token bucket to control
	 * the data flow rate. It prevents the operation from transferring data too
	 * fast.
	 *
	 * The next step in the preparation is then to configure the playback device
	 * for the pipe. It ensure that data from the capture device to be compatible
	 * to the pipe. Note that the operation will configure a single period of
	 * prebuffer time, so that the playback device will start the playback only
	 * after at least a period of audio data is written to it.
	 *
	 * Finally, to finish the preparation and start the transfer, both the token
	 * bucket and the playback device will be started.
	 *
	 * The transfer phase runs in a loop and each loop will perform an action
	 * depending on whether the token bucket is empty. If the token bucket is
	 * empty, the operation should wait until the bucket is refilled. If the
	 * outlet has no readable block after the current read position, the
	 * operation should wait until a new block arrives to the outlet. If the
	 * current read position is too far behind, the operation should fail with
	 * an data loss exception. Otherwise, a single period of audio data is
	 * copied from the outlet to the playback device, the current read position
	 * is advanced and a token is deducted from the token bucket.
	 *
	 * The transfer phase can be terminated when any exception is thrown. When
	 * the transfer finishes, cleanup is done by stopping the capture device
	 * and the token bucket.
	 *
	 * Throughout the operation, appropriate callback will be invoked to report
	 * the progress. They can be used to stop the operation by throwing
	 * exceptions.
	 *
	 * Issues
	 * ======
	 *
	 * The one-period prebuffer time means that drain operation will introduce
	 * a period of latency to the whole audio pipeline.
	 *
	 * Next, the one-period prebuffer may not be enough when the feeder of the
	 * pipe flushes block with high jitter. It can lead to buffer undeerun and
	 * choppy audio. This is the exact reason why the ALSA piper playback device
	 * uses thread to ensure steady data transfer. It may be better if the.
	 * prebuffer duration can be adjusted.
	 *
	 */
	class DrainOperation
	{
		public:

			/**
			 * Construct a new drain operation.
			 */
			explicit DrainOperation(Callback& callback) : m_callback(callback) {}

			/**
			 * Get the callback used in the drain operation.
			 */
			const Callback& callback() const noexcept { return m_callback; }

			/**
			 * Execute the drain operation. This operation may throw exception
			 * when the operation cannot keep up with incoming data from the pipe
			 * data is lost. Also, any exception thrown by the callback will stop
			 * the operation and it will then be rethrown verbatim.
			 */
			void execute(Pipe& pipe, PlaybackDevice& device);

		private:

			/**
			 * Callback invoked during the operation.
			 */
			Callback& m_callback;

	};

	/**
	 * Exception thrown when the draining operation cannot keep up with incoming
	 * data from the pipe and data is lost.
	 */
	class DrainDataLossException : public Exception
	{
		public:
			using Exception::Exception;
	};

};


#endif


