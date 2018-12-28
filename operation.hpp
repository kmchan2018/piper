

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
			 * Called whenever a transfer from/into the pipe occured.
			 */
			virtual void on_transfer(const Preamble& preamble, const Buffer& buffer) = 0;

			/**
			 * Called whenever an operation reaches a point where its execution
			 * can be interrupted.
			 */
			virtual void on_tick() = 0;

	};

	/**
	 * Feed operation feeds data from a capture device to a pipe.
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

			Callback& m_callback;

	};

	/**
	 * Drain operation drains data from a pipe device to a playback device.
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


