

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "alsa.hpp"
#include "timestamp.hpp"
#include "pipe.hpp"
#include "signpost.hpp"
#include "timer.hpp"


/**
 * This class implements a ALSA PCM playback device that writes audio data to
 * the specified pipe file.
 *
 * Overview
 * ========
 *
 * The playback device created from this class will accept only audio data
 * matching the pipe file specification, including channel count, sample
 * format and period size. As for buffer size, the device will accept any
 * buffer size up to the pipe write window.
 *
 * For the software parameters, the device will honor the specified start
 * threshold. However, both avail_min and period_event are ignored, and the
 * device will behave as if avail_min is set to 1 and period_event is set to
 * enable - it means that the device descriptor will be writable whenever
 * space is available in the device buffer.
 *
 * The device will spawn a new thread when it is opened. The thread is
 * responsible for flusing blocks into the pipe every period. The thread
 * will be stopped when the device is closed.
 *
 * Design Rationale
 * ================
 *
 * The use of thread in the class requires some explanation. Without the
 * pump thread, one can flush block in the pointer callback, but the flush
 * rate is now controlled by the client. For some clients, it can lead to
 * high jitter in the pipe data transfer which can cause trouble on the
 * drainer side.
 *
 * Implementation Details
 * ======================
 *
 * The device is implemented as an ALSA IOPlug device using the API defined
 * in alsa.hpp and alsa.cpp. This class extends the Handler class and
 * implement the required callbacks to handle device operations from the
 * clients. See the comments on each methods for more information.
 *
 * The class expects a few invariants to be upheld. Some of them are not
 * obvious and therefore should be documented here for reference:
 *
 * The first invariant is that the first writable block of the pipe should
 * correspond to the device hardware pointer plus `m_expiration` periods
 * while the device is in the PREPARED or RUNNING state. The invariant is
 * essential for data transfer and violation can cause corruption of audio
 * data.
 *
 * The next invariant is that the buffer size stored in the class should
 * match the real device buffer of the class. The size stored in the class
 * is used to silence new writable blocks in the pipe. Violation of the
 * invariant can cause intermittent silence in the drainer side.
 *
 * Another invariant is that the timer should have the same period as the
 * pipe. The timer controls block flushes. Violation of the invariant can
 * cause buffer underrun of the client as well as timing issue in the drainer
 * side.
 *
 * Last but not least, the signpost should be activated when the device
 * buffer has free space, deactivated otherwise. The signpost is polled by
 * some clients to schedule audio data delivery, so violation of the invariant
 * may prevent some clients from writing audio data to the device. It should
 * also be noted that this invariant means that every period the signpost will
 * be activated because the pump thread will flush blocks into the pipe and
 * creates free space in the device buffer.
 *
 */
class PiperPlaybackHandler : public ALSA::IOPlug::Implementation
{
	public:

		/**
		 * Enum for pump thread status. The pump thread will check the flag every
		 * so often to determine actions it need to take apart from the check. IDLE
		 * indicates that the pump thread should do nothing else; ACTIVE indicates
		 * that the pump thread should periodically flush blocks down the pipe; END
		 * indicates that the pump thread should end.
		 */
		enum class Status { IDLE = 0, ACTIVE = 1, END = 2 };

		/**
		 * Construct a new piper playback handler.
		 */
		PiperPlaybackHandler(const char* path) :
			m_pipe(path),
			m_inlet(m_pipe),
			m_timer(m_pipe.period_time()),
			m_signpost(),
			m_buffer(0),
			m_expirations(0),
			m_transfer_source(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_transfer_target(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_status(Status::IDLE),
			m_mutex(),
			m_pump(&PiperPlaybackHandler::pump, this)
		{
			// do nothing
		}

		/**
		 * Configure the playback device before its creation.
		 */
		void configure(const char* name, [[ gnu::unused ]] snd_pcm_stream_t stream, [[ gnu::unused ]] int mode, ALSA::IOPlug::Options& options)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			options.name = name;
			options.poll_fd = m_signpost.descriptor();
			options.poll_events = POLLIN;
			options.enable_prepare_callback = true;
			options.enable_poll_descriptors_count_callback = true;
			options.enable_poll_descriptors_callback = true;
			options.enable_poll_revents_callback = true;
			options.enable_transfer_callback = true;
		}

		/**
		 * Limits the hardware parameter the playback device accepts. The method
		 * will extract the audio data specification from the pipe and enforce
		 * them on the device.
		 */
		void create(ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(m_pipe.format_code_alsa()) };
			unsigned int channels_list[] = { m_pipe.channels() };
			unsigned int rate_list[] = { m_pipe.rate() };
			unsigned int period_list[] = { static_cast<unsigned int>(m_pipe.period_size()) };

			control.set_parameter_list(SND_PCM_IOPLUG_HW_ACCESS, 2, access_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_FORMAT, 1, format_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_RATE, 1, rate_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list);
			control.set_parameter_range(SND_PCM_IOPLUG_HW_PERIODS, 2, m_inlet.window());
		}

		/**
		 * Prepare the playback device. After this call, the device will enter
		 * PREPARED state. At this point, the device should have an empty buffer
		 * and hence it should be able to receive audio data from the application.
		 *
		 * First of all, this callback will reset the status to IDLE to ensure
		 * that the pump thread will not continue to flush blocks down the pipe.
		 * It makes sure that audio data written into the device will not be
		 * played before the playback starts.
		 *
		 * Additionally, this callback will also reset the expiration count to
		 * zero. At this point, the device hardware pointer will reset. The action
		 * will ensure that the new hardware pointer will align with the first
		 * writable blocks of the pipe.
		 *
		 * Next, the buffer should be cleared. As blocks are flushed to the pipe
		 * continuously by the pump thread, an underrunning playback device may
		 * deliver stale data until one of the callback is invoked and idling the
		 * pump thread. Clearing the buffer prevents flushing down stale data as
		 * if they are new.
		 *
 		 * Finally, this callback will activate the signpost to report that
		 * the device can be written to.
		 */
		void prepare(ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			m_status = Status::IDLE;
			m_buffer = control.buffer_size() / control.period_size();
			m_expirations = 0;
			m_signpost.activate();

			const Piper::Inlet::Position clear_start = m_inlet.start();
			const Piper::Inlet::Position clear_until = clear_start + m_buffer;

			for (Piper::Inlet::Position position = clear_start; position < clear_until; position++) {
				Piper::Buffer buffer = m_inlet.content(position);
				std::memset(buffer.start(), 0, buffer.size());
			}
		}

		/**
		 * Start the playback device. After this call, the device will move to
		 * RUNNING state. In this state, the device should periodically deliver
		 * audio data in the device buffer into the pipe.
		 *
		 * This callback will update status to ACTIVE and start the timer. It 
		 * ensure that the pump thread will watch the timer and periodically
		 * flush blocks into the pipe.
		 */
		void start([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			m_status = Status::ACTIVE;
			m_timer.start();
		}

		/**
		 * Stop the playback in the playback device. After this call, the device
		 * will return to SETUP state, and should stop accepting audio data nor
		 * delivering them into the pipe.
		 *
		 * This callback will reset the status back to IDLE and stop the timer.
		 * It ensures that audio data will no longer be flushed into the pipe.
		 * 
		 * Additionally, this callback will deactivate the signpost to reflect
		 * the fact that the device is no longer accepting audio data.
		 */
		void stop([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			m_timer.stop();
			m_signpost.deactivate();
			m_expirations = 0;
			m_buffer = 0;
			m_status = Status::IDLE;
		}

		/**
		 * Return the number of descriptors that should be polled by the client
		 * for device events. This callback will return 1 for signpost descriptor.
		 */
		int poll_descriptors_count([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			return 1;
		}

		/**
		 * Return the details of descriptors that should be polled by the client
		 * for device events. See the other poll callbacks for more information.
		 */
		int poll_descriptors([[ gnu::unused ]] ALSA::IOPlug::Control& control, struct pollfd* pfd, unsigned int space)
		{
			assert(pfd != nullptr);
			assert(space >= 1);

			std::lock_guard<std::mutex> guard(m_mutex);

			pfd[0].fd = m_signpost.descriptor();
			pfd[0].events = POLLIN;
			pfd[0].revents = 0;

			return 1;
		}

		/**
		 * Check the poll result and return the device events. Note that the
		 * callback will demangle the event code and translate POLLIN events to
		 * POLLOUT events. It is because signpost descriptor can only be polled
		 * for POLLIN events but the application expects POLLOUT events.
		 */
		void poll_revents([[ gnu::unused ]] ALSA::IOPlug::Control& control, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
		{
			assert(pfd != nullptr);
			assert(revents != nullptr);
			assert(nfds >= 1);

			std::lock_guard<std::mutex> guard(m_mutex);

			for (unsigned int i = 0; i < nfds; i++) {
				unsigned short temp = static_cast<unsigned short>(pfd[i].revents);

				if (temp != 0 && (temp & POLLIN) != 0) {
					*revents = (temp & ~POLLIN) | POLLOUT;
					return;
				} else if (temp != 0) {
					*revents = temp;
					return;
				}
			}
		}

		/**
		 * Return the hardware pointer of the playback device. It is called when
		 * the device is in PREPARED or RUNNING state to check the position of
		 * hardware pointer.
		 *
		 * First of all, this callback will retrieve the buffer usage according to
		 * the current hardware pointer, plus the number of frames flushed by the
		 * pump thread but unaccounted for by the current hardware pointer.
		 *
		 * If the buffer usage is less than the flushed frame, the pump thread
		 * should have pumped past the current application pointer and buffer
		 * underrun occured. In such case, stop the playback and report the error.
		 *
		 * If everything is OK, the callback will take a few actions. It should
		 * reset the expiration count to zero since the hardware pointer and the
		 * first writable block of the pipe should once again align.
		 */
		snd_pcm_uframes_t pointer(ALSA::IOPlug::Control& control)
		{
			std::lock_guard<std::mutex> guard(m_mutex);

			const snd_pcm_uframes_t period = control.period_size();
			const snd_pcm_uframes_t used = control.buffer_used();
			const snd_pcm_uframes_t flushed = m_expirations * period;

			m_expirations = 0;

			if (used < flushed) {
				SNDERR("device cannot be polled: underrun");
				m_timer.stop();
				m_signpost.deactivate();
				m_expirations = 0;
				m_buffer = 0;
				m_status = Status::IDLE;
				throw ALSA::xrun_error();
			}

			return control.calculate_next_hardware_pointer(flushed);
		}

		/**
		 * Transfer data into the device buffer starting at the current application
		 * pointer.
		 *
		 * First of all, this callback will retrieve the buffer usage according to
		 * the current hardware pointer, plus the number of frames flushed by the
		 * pump thread but unaccounted for by the current hardware pointer.
		 *
		 * If the buffer usage is less than the flushed frame, the pump thread
		 * should have pumped past the current application pointer and buffer
		 * underrun occured. In such case, stop the playback and report the error.
		 *
		 * After that, this callback will calculate the position of writable block
		 * corresponding to the current application pointer. It will also restrict
		 * the amount of data to be copied should the buffer has limited space.
		 * After that, the audio data is copied.
		 *
		 * Before returning, the signpost should be updated to reflect the amount
		 * of free space still available for transfer.
		 *
		 * Finally, the amount of data copied is returned.
		 */
		snd_pcm_uframes_t transfer(ALSA::IOPlug::Control& control, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
		{
			assert(areas != nullptr);
			assert(size > 0);

			std::lock_guard<std::mutex> guard(m_mutex);

			const snd_pcm_uframes_t period = control.period_size();
			const snd_pcm_uframes_t used = control.buffer_used();
			const snd_pcm_uframes_t free = control.buffer_free();
			const snd_pcm_uframes_t flushed = m_expirations * period;

			if (used < flushed) {
				SNDERR("device cannot be written: underrun");
				m_timer.stop();
				m_signpost.deactivate();
				m_expirations = 0;
				m_buffer = 0;
				m_status = Status::IDLE;
				throw ALSA::xrun_error();
			}

			Piper::Inlet::Position increment = used / period;
			Piper::Inlet::Position block = m_inlet.start() - m_expirations + increment;
			Piper::Buffer buffer = m_inlet.content(block);
			snd_pcm_uframes_t pending = size;
			snd_pcm_uframes_t done = 0;

			if (pending > free) {
				SNDERR("device cannot be written: insufficient space (%lu) for incoming data (%lu)", free, size);
				pending = free;
			}

			m_transfer_source.reset(areas, offset + size);
			m_transfer_target.reset(buffer.start(), buffer.size());
			m_transfer_source.behead(offset);
			m_transfer_target.behead(used - increment * period);

			assert(m_transfer_source.valid());
			assert(m_transfer_target.valid());

			while (pending > 0) {
				if (m_transfer_target.valid() == false) {
					buffer = m_inlet.content(++block);
					m_transfer_target.reset(buffer.start(), buffer.size());
				}

				assert(m_transfer_source.valid());
				assert(m_transfer_target.valid());

				snd_pcm_uframes_t copied = ALSA::Range::copy_behead(m_transfer_target, m_transfer_source, pending);
				done += copied;
				pending -= copied;
			}

			if (free + flushed - done > 0) {
				m_signpost.activate();
			} else {
				m_signpost.deactivate();
			}

			return done;
		}

		/**
		 * Pump thread should periodically check the status and pump blocks down
		 * the pipe when active.
		 *
		 * Logically, the thread should check the current status in a loop. When
		 * the status is END, the thread should end the loop and return. When the
		 * status is IDLE, the thread should wait for a short interval in the loop
		 * body; when the status is ACTIVE, the thread should poll the timer for
		 * for updates with a fixed timeout, and if the timer fires the thread
		 * should flush blocks down the pipe. Along with the flush, signpost should
		 * be activated to indicate free space for new audio data.
		 *
		 * In practice, the loop is implemented differently but gives the same
		 * behavior. The timer is polled for updates in the loop with timeout
		 * when the status is IDLE or ACTIVE - that means both statuses uses the
		 * same code path for waiting. Also note that the timer descriptor and
		 * pipe period are read and polled without locking: these are logically
		 * immutable throughout the device lifetime and therefore should not 
		 * cause thread-safety issues.
		 *
		 * Also, note that new writable blocks from the pipe are cleared. The
		 * reason is already outlined in the prepare method, so it is not
		 * repeated here.
		 */
		void pump()
		{
			int descriptor = m_timer.descriptor();
			int timeout = m_pipe.period_time() / 1000000L;

			struct pollfd pfd;
			pfd.fd = descriptor;
			pfd.events = POLLIN;
			pfd.revents = 0;

			while (m_status != Status::END) {
				if (::poll(&pfd, 1, timeout) > 0 && (pfd.revents & POLLIN) > 0) {
					if (m_status == Status::ACTIVE) {
						std::lock_guard<std::mutex> guard(m_mutex);

						m_timer.try_accumulate(0);

						const unsigned int outstanding = m_timer.consume();
						const Piper::Inlet::Position flush_start = m_inlet.start();
						const Piper::Inlet::Position flush_until = flush_start + outstanding;
						const Piper::Inlet::Position clear_start = m_inlet.start() + m_buffer;
						const Piper::Inlet::Position clear_until = clear_start + outstanding;

						for (Piper::Inlet::Position position = flush_start; position < flush_until; position++) {
							m_inlet.preamble(position).timestamp = Piper::now();
							m_inlet.flush();
						}

						for (Piper::Inlet::Position position = clear_start; position < clear_until; position++) {
							Piper::Buffer buffer = m_inlet.content(position);
							std::memset(buffer.start(), 0, buffer.size());
						}

						m_expirations += outstanding;

						if (outstanding > 0) {
							m_signpost.activate();
						}
					}
				}
			}
		}

		/**
		 * Close the device. This callback will update the status to END and wait
		 * until the pump thread finishes.
		 */
		void close([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			m_status = Status::END;
			m_pump.join();
		}

	private:

		/**
		 * Pipe where data is written to.
		 */
		Piper::Pipe m_pipe;

		/**
		 * Inlet where data is written to. Note that creating an inlet over a pipe
		 * will lock the pipe and blocking other inlets over the same pipe in the
		 * whole system.
		 */
		Piper::Inlet m_inlet;

		/**
		 * Timer for triggering periodic block flushes. Its period should be the
		 * same as the pipe period.
		 */
		Piper::Timer m_timer;

		/**
		 * Signpost for reporting device availability. It should be activated when
		 * there are space in the device buffer for writing, deactivated otherwise.
		 */
		Piper::SignPost m_signpost;

		/**
		 * Size of the playback device buffer in periods. It is used to determine
		 * new writable pipe blocks that have to be cleared.
		 */
		Piper::Inlet::Position m_buffer;

		/**
		 * Number of periods flushed into the pipe that are not yet acknowledged
		 * in the hardware pointer. It is used to calculate hardware pointer updates
		 * as well as to translate application pointers to corresponding pipe
		 * positions.
		 */
		Piper::Inlet::Position m_expirations;

		/**
		 * ALSA range where data is transferred from. It helps with data copy in
		 * the transfer method.
		 */
		ALSA::Range m_transfer_source;

		/**
		 * ALSA range where data is transferred to. It helps with data copy in
		 * the transfer callback.
		 */
		ALSA::Range m_transfer_target;

		/**
		 * Status of the pump thread. It is atomic to ensure that the status
		 * can be checked without locking.
		 */
		std::atomic<Status> m_status;

		/**
		 * Mutex to ensure that only a single thread (either the client thread
		 * or the pump thread) can access the internals.
		 */
		std::mutex m_mutex;

		/**
		 * Handle to the pump thread which can be used to control it.
		 */
		std::thread m_pump;

};


/**
 * This class implements a ALSA PCM IOPlug device that reads audio data from
 * the specified pipe file.
 *
 * Overview
 * ========
 *
 * The capture device created from this class will return only audio data
 * matching the pipe file specification, including channel count, sample
 * format and period size. As for buffer size, the device will accept any
 * buffer size up to the pipe read window.
 *
 * For the software parameters, the device will ignore both avail_min and
 * period_event, and the device will behave as if avail_min is set to 1
 * and period_event is set to enable - it means that the device descriptors
 * will be readable whenever data is available in the device buffer.
 *
 * Unlike the playback counterpart, this device will not spawn new thread.
 *
 * Implementation Details
 * ======================
 *
 * The device is implemented as an ALSA IOPlug device using the API defined
 * in alsa.hpp and alsa.cpp. This class extends the Handler class and
 * implement the required callbacks to handle device operations from the
 * clients. See the comments on each methods for more information.
 *
 * The class expects a few invariants to be upheld. Some of them are not
 * obvious and therefore should be documented here for reference:
 *
 * The first invariant is that the current hardware pointer should align
 * with the cursor stored in the class. The invariant is used to position
 * reads and, violation will lead to corrupted audio data.
 *
 * Another invariant is that the timer should have the same period as the
 * pipe. The timer alerts the client of elapsed period and possible arrival
 * of audio data. Violation of the invariant can cause buffer overrun of the
 * client.
 *
 * Last but not least, the signpost should be activated when the device
 * buffer has pending data, deactivated otherwise. The signpost is polled
 * by some clients to schedule audio data retrieval, so violation of the
 * may prevent some clients from reading audio data from the device. Unlike
 * the playback counterpart, the signpost is not activated every period.
 *
 */
class PiperCaptureHandler : public ALSA::IOPlug::Implementation
{
	public:

		/**
		 * Construct a new piper capture handler.
		 */
		PiperCaptureHandler(const char* path) :
			m_pipe(path),
			m_outlet(m_pipe),
			m_timer(m_pipe.period_time()),
			m_signpost(),
			m_cursor(m_outlet.until()),
			m_transfer_source(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_transfer_target(m_pipe.format_code_alsa(), m_pipe.channels())
		{
			// do nothing
		}

		/**
		 * Configure the capture device before its creation.
		 */
		void configure(const char* name, [[ gnu::unused ]] snd_pcm_stream_t stream, [[ gnu::unused ]] int mode, ALSA::IOPlug::Options& options)
		{
			options.name = name;
			options.enable_prepare_callback = true;
			options.enable_poll_descriptors_count_callback = true;
			options.enable_poll_descriptors_callback = true;
			options.enable_poll_revents_callback = true;
			options.enable_transfer_callback = true;
		}

		/**
		 * Limits the hardware parameter the capture device accepts. The method
		 * will extract the audio data specification from the pipe and enforce
		 * them on the device.
		 */
		void create(ALSA::IOPlug::Control& control)
		{
			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(m_pipe.format_code_alsa()) };
			unsigned int channels_list[] = { m_pipe.channels() };
			unsigned int rate_list[] = { m_pipe.rate() };
			unsigned int period_list[] = { static_cast<unsigned int>(m_pipe.period_size()) };

			control.set_parameter_list(SND_PCM_IOPLUG_HW_ACCESS, 2, access_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_FORMAT, 1, format_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_RATE, 1, rate_list);
			control.set_parameter_list(SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list);
			control.set_parameter_range(SND_PCM_IOPLUG_HW_PERIODS, 2, m_outlet.window());
		}

		/**
		 * Prepare the capture device. After this call, the device will enter the
		 * PREPARED state. At this point, the device should have an empty buffer.
		 * Note that unlike playback device, it means that no audio data can be
		 * read from the device.
		 *
 		 * This callback will deactivate the signpost to report that the device
		 * cannot be read from yet.
		 */
		void prepare([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			m_signpost.deactivate();
		}

		/**
		 * Start the capture device. After this call, the device will enter the
		 * RUNNING state. In this state, the device should periodically deliver
		 * audio data from the pipe to the device buffer for reading.
		 *
		 * This callback will start the timer to signal possible data arrival and
		 * hardware pointer updates. It will also initialize the cursor to point
		 * to the end of pipe read window.
		 */
		void start([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			m_timer.start();
			m_cursor = m_outlet.until();
		}

		/**
		 * Stop the capture device. After this call, the device will enter the SETUP
		 * state. In this state, the device will no longer deliver audio data from
		 * the pipe to the device buffer and then to the application.
		 *
		 * The callback will stop the timer and deactivate the signpost to reflect
		 * the situation.
		 */
		void stop([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			m_timer.stop();
			m_signpost.deactivate();
		}

		/**
		 * Return the number of descriptors that should be polled by the client
		 * for device events.
		 *
		 * The callback will return 2 for descriptors from both timer and signpost.
		 * The timer descriptor is responsible for signalling each lapse of period
		 * and possible hardware pointer changes; on the other hand, the signpost
		 * descriptor is responsible for advertising availability of data in the
		 * device buffer and hence opportunities of non-blocking reads.
		 */
		int poll_descriptors_count([[ gnu::unused ]] ALSA::IOPlug::Control& control)
		{
			return 2;
		}

		/**
		 * Return the details of descriptors that should be polled by the client
		 * for device events. See the other poll callbacks for more information.
		 */
		int poll_descriptors([[ gnu::unused ]] ALSA::IOPlug::Control& control, struct pollfd* pfd, unsigned int space)
		{
			assert(pfd != nullptr);
			assert(space >= 2);

			pfd[0].fd = m_timer.descriptor();
			pfd[0].events = POLLIN;
			pfd[0].revents = 0;
			pfd[1].fd = m_signpost.descriptor();
			pfd[1].events = POLLIN;
			pfd[1].revents = 0;

			return 2;
		}

		/**
		 * Check the poll result and return the device events. Note that the
		 * callback will not need to mangle event codes.
		 */
		void poll_revents([[ gnu::unused ]] ALSA::IOPlug::Control& control, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
		{
			assert(pfd != nullptr);
			assert(revents != nullptr);
			assert(nfds >= 2);

			for (unsigned int i = 0; i < nfds; i++) {
				if (pfd[i].revents != 0) {
					*revents = static_cast<unsigned short>(pfd[i].revents);
					return;
				}
			}
		}

		/**
		 * Return the hardware pointer of the capture device. It is called when the
		 * device is in PREPARED or RUNNING state to check the position of hardware
		 * pointer.
		 *
		 * The callback will update the cursor to point to the end of pipe read
		 * window. It will also detect possible xrun and handle it as well. The
		 * process involves:
		 *
		 * 1. Check the device for amount of space available for incoming data.
		 * 2. Check the pipe for amount of data available to fetch.
		 * 3. Report buffer overrun if there are not enough space for incoming data.
		 * 4. Update the cursor to the end of the pipe read window.
		 * 5. Activate the signpost if new data becomes available.
		 * 6. Activate the signpost if some data is available even without new data.
		 * 7. Deactivate the signpost otherwise.
		 * 8. Calculate the new hardware pointer and return it.
		 */
		snd_pcm_uframes_t pointer(ALSA::IOPlug::Control& control)
		{
			const snd_pcm_uframes_t period = control.period_size();
			const snd_pcm_uframes_t used = control.buffer_used();
			const snd_pcm_uframes_t free = control.buffer_free();

			m_timer.try_accumulate(0);
			m_timer.consume();

			const Piper::Outlet::Position until = m_outlet.until();
			const Piper::Outlet::Position delta = until - m_cursor;
			const snd_pcm_uframes_t incoming = period * delta;

			if (incoming > free) {
				m_timer.stop();
				m_signpost.deactivate();
				throw ALSA::xrun_error();
			} else if (incoming > 0) {
				m_cursor = until;
				m_signpost.activate();
			} else if (used > 0) {
				m_signpost.activate();
			} else {
				m_signpost.deactivate();
			}

			return control.calculate_next_hardware_pointer(incoming);
		}

		/**
		 * Transfer data out of the buffer of the capture device. It is called
		 * when the device is in RUNNING state in response to reads from the
		 * application.
		 *
		 * This callback will copy existing audio data from the device buffer to
		 * the application buffer. The process involves:
		 *
		 * 1. Calculate the amount of readable data in the device buffer.
		 * 2. Limit the output size to the amount of readable data.
		 * 3. Calculate the position of first block where readable data is at.
		 * 4. Calculate the offset in the first block where readable data is at.
		 * 5. Copy audio data to the destination, moving to new blocks when needed.
		 * 6. Deactivate the signpost if no data is readable from the start.
		 * 6. Deactivate the signpost if no data is readable after the copy.
		 * 7. Activate the signpost otherwise.
		 * 8. Return the amount of frames copied.
		 */
		snd_pcm_uframes_t transfer(ALSA::IOPlug::Control& control, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
		{
			assert(areas != nullptr);
			assert(size > 0);

			const snd_pcm_uframes_t period = control.period_size();
			const snd_pcm_uframes_t used = control.buffer_used();
			
			Piper::Outlet::Position decrement = used / period + (used % period > 0 ? 1 : 0);
			Piper::Outlet::Position block = m_cursor - decrement;
			Piper::Buffer buffer = m_outlet.content(block);
			snd_pcm_uframes_t pending = size;
			snd_pcm_uframes_t done = 0;

			m_transfer_source.reset(buffer.start(), buffer.size());
			m_transfer_target.reset(areas, offset + size);
			m_transfer_source.behead(decrement * period - used);
			m_transfer_target.behead(offset);

			if (pending > used) {
				SNDERR("device cannot be written: insufficient data (%lu) than requested (%lu)", used, size);
				pending = used;
			}

			assert(m_transfer_source.valid());
			assert(m_transfer_target.valid());

			while (pending > 0) {
				if (m_transfer_source.valid() == false) {
					buffer = m_outlet.content(++block);
					m_transfer_source.reset(buffer.start(), buffer.size());
				}

				assert(m_transfer_source.valid());
				assert(m_transfer_target.valid());

				snd_pcm_uframes_t copied = ALSA::Range::copy_behead(m_transfer_target, m_transfer_source, pending);
				done += copied;
				pending -= copied;
			}

			if (used == 0) {
				m_signpost.deactivate();
			} else if (used == done) {
				m_signpost.deactivate();
			} else {
				m_signpost.activate();
			}

			return done;
		}

	private:

		/**
		 * Pipe where data is read from.
		 */
		Piper::Pipe m_pipe;

		/**
		 * Outlet where data is read from.
		 */
		Piper::Outlet m_outlet;

		/**
		 * Timer for triggering period events. Its period should be the same as
		 * the pipe period.
		 */
		Piper::Timer m_timer;

		/**
		 * Signpost for reporting device availability. It should be activated when
		 * there are data in the device buffer for reading, deactivated otherwise.
		 */
		Piper::SignPost m_signpost;

		/**
		 * Pipe block that corresponds to the current device hardware pointer.
		 * It is used to translate application pointers to corresponding pipe
		 * positions.
		 */
		Piper::Outlet::Position m_cursor;

		/**
		 * ALSA range where data is transferred from. It helps with data copy in
		 * the transfer callback.
		 */
		ALSA::Range m_transfer_source;

		/**
		 * ALSA range where data is transferred to. It helps with data copy in
		 * the transfer callback.
		 */
		ALSA::Range m_transfer_target;

};


extern "C"
{

	/**
	 * Open the piper device. The function will parse the device configuration
	 * and initialize the appropriate IOPlug device according to the stream
	 * type.
	 */
	SND_PCM_PLUGIN_DEFINE_FUNC(piper)
	{
		snd_config_iterator_t i, next;
		const char* path;

		snd_config_for_each(i, next, conf) {
			snd_config_t* n = snd_config_iterator_entry(i);
			const char* id;

			if (snd_config_get_id(n, &id) < 0) {
				continue;
			} else if (std::strcmp(id, "comment") == 0 || std::strcmp(id, "type") == 0 || std::strcmp(id, "hint") == 0) {
				continue;
			} else if (std::strcmp(id, "playback") == 0) {
				if (stream != SND_PCM_STREAM_PLAYBACK) {
					continue;
				} else if (snd_config_get_string(n, &path) < 0) {
					SNDERR("device %s cannot be initialized: invalid playback %s in config", name, path);
					return -EINVAL;
				}
			} else if (std::strcmp(id, "capture") == 0) {
				if (stream != SND_PCM_STREAM_CAPTURE) {
					continue;
				} else if (snd_config_get_string(n, &path) < 0) {
					SNDERR("device %s cannot be initialized: invalid capture %s in config", name, path);
					return -EINVAL;
				}
			} else {
				SNDERR("device %s cannot be initialized: unknown field %s in config", name, id);
				return -EINVAL;
			}
		}

		try {
			if (stream == SND_PCM_STREAM_PLAYBACK) {
				ALSA::IOPlug ioplug{name, stream, mode, std::unique_ptr<ALSA::IOPlug::Implementation>(new PiperPlaybackHandler(path))};
				*pcmp = ioplug.release();
				return 0;
			} else if (stream == SND_PCM_STREAM_CAPTURE) {
				ALSA::IOPlug ioplug{name, stream, mode, std::unique_ptr<ALSA::IOPlug::Implementation>(new PiperCaptureHandler(path))};
				*pcmp = ioplug.release();
				return 0;
			} else {
				SNDERR("device %s cannot be initialized: device supports playback only", name);
				return -EINVAL;
			}
		} catch (std::system_error& ex) {
			int code = ex.code().value();
			SNDERR("device %s cannot be opened: %s", name, ex.what());
			return (code < 0 ? code : -code);
		} catch (Piper::FileNotExistException& ex) {
			SNDERR("device %s cannot be opened: pipe file cannot be found", name);
			return -EINVAL;
		} catch (Piper::PipeCorruptedException& ex) {
			SNDERR("device %s cannot be opened: pipe file corrupted", name);
			return -EINVAL;
		} catch (Piper::PipeConcurrentInletException& ex) {
			SNDERR("device %s cannot be opened: pipe file already in use", name);
			return -EBUSY;
		} catch (std::bad_alloc& ex) {
			SNDERR("device %s cannot be opened: memory allocation error", name);
			return -ENOMEM;
		} catch (std::invalid_argument& ex) {
			SNDERR("device %s cannot be opened: logic error in underlying component", name);
			return -EIO;
		} catch (std::logic_error& ex) {
			SNDERR("device %s cannot be opened: logic error in underlying component", name);
			return -EIO;
		} catch (...) {
			SNDERR("device %s cannot be opened: unknown error", name);
			return -EIO;
		}
	}

	SND_PCM_PLUGIN_SYMBOL(piper)

}


