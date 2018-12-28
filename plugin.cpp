

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <system_error>
#include <utility>

#include "alsa.hpp"
#include "exception.hpp"
#include "timestamp.hpp"
#include "pipe.hpp"
#include "signpost.hpp"
#include "timer.hpp"


/**
 * This class handles the callbacks of piper plugin over playback stream.
 */
class PiperPlaybackHandler : public ALSA::IOPlug::Handler
{
	public:

		/**
		 * Construct a new piper playback handler.
		 */
		PiperPlaybackHandler(const char* path) :
			m_pipe(path),
			m_inlet(m_pipe),
			m_timer(m_pipe.period_time()),
			m_signpost(),
			m_transfer_source(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_transfer_target(m_pipe.format_code_alsa(), m_pipe.channels())
		{
			// do nothing
		}

		/**
		 * Configure the playback device before its creation.
		 */
		void configure(const char* name, snd_pcm_stream_t stream, int mode, ALSA::IOPlug::Options& options)
		{
			options.name = name;
			options.enable_prepare_callback = true;
			options.enable_poll_descriptors_count_callback = true;
			options.enable_poll_descriptors_callback = true;
			options.enable_poll_revents_callback = true;
			options.enable_transfer_callback = true;
		}

		/**
		 * Limits the hardware parameter the playback device accepts.
		 */
		void create(ALSA::IOPlug& ioplug)
		{
			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(m_pipe.format_code_alsa()) };
			unsigned int channels_list[] = { m_pipe.channels() };
			unsigned int rate_list[] = { m_pipe.rate() };
			unsigned int period_list[] = { static_cast<unsigned int>(m_pipe.period_size()) };

			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_ACCESS, 2, access_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_FORMAT, 1, format_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_RATE, 1, rate_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list);
			ioplug.set_parameter_range(SND_PCM_IOPLUG_HW_PERIODS, 2, m_inlet.window());
		}

		/**
		 * Prepare the playback device. After this call, the device will enter
		 * PREPARED state. At this point, the device should have an empty buffer
		 * and hence it should be able to receive audio data from the application.
		 *
 		 * This callback will activate the signpost to report that the device can
		 * be written to.
		 */
		void prepare(ALSA::IOPlug& ioplug)
		{
			m_signpost.activate();
		}

		/**
		 * Start the playback device. After this call, the device will move to
		 * RUNNING state. In this state, the device should periodically deliver
		 * audio data in the device buffer into the pipe.
		 *
		 * This callback will start the timer to signal possible hardware pointer
		 * updates every period. The application will then handle the signals and
		 * deliver audio data via the pointer callback.
		 */
		void start(ALSA::IOPlug& ioplug)
		{
			m_timer.start();
		}

		/**
		 * Stop the playback in the playback device. After this call, the device
		 * willreturn to SETUP state, and should stop accepting audio data nor
		 * delivering them into the pipe.
		 *
		 * This callback will stop the timer and deactivate the signpost to
		 * reflect the situation.
		 */
		void stop(ALSA::IOPlug& ioplug)
		{
			m_timer.stop();
			m_signpost.deactivate();
		}

		/**
		 * Return the number of descriptors that should be polled by the client
		 * for device events.
		 *
		 * This callback will return 2 for descriptors from both timer and signpost.
		 * The timer descriptor is responsible for signalling each lapse of period
		 * and possible hardware pointer changes; on the other hand, the signpost
		 * descriptor is responsible for advertising availability of space in the
		 * device buffer and hence opportunities of non-blocking writes.
		 */
		int poll_descriptors_count(ALSA::IOPlug& ioplug)
		{
			return 2;
		}

		/**
		 * Return the details of descriptors that should be polled by the client
		 * for device events. See the other poll callbacks for more information.
		 */
		int poll_descriptors(ALSA::IOPlug& ioplug, struct pollfd* pfd, unsigned int space)
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
		 * callback will demangle the event code and translate POLLIN events to
		 * POLLOUT events. It is because both timer and signpost descriptors can
		 * only be polled for POLLIN events but the application expects POLLOUT
		 * events.
		 */
		void poll_revents(ALSA::IOPlug& ioplug, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
		{
			assert(pfd != nullptr);
			assert(revents != nullptr);
			assert(nfds >= 2);

			for (unsigned int i = 0; i < nfds; i++) {
				unsigned short temp = pfd[i].revents;

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
		 * This callback will deliver audio data into the pipe and return back the
		 * updated hardware pointer. It will also detect possible xrun and handle
		 * it as well. The process involves:
		 *
		 * 1. Check the device for amount of data available for delivery.
		 * 2. Check the timer for amount of data that should be delivered.
		 * 3. Report buffer underrun if insufficient data is available.
		 * 4. Timestamp of relevant writable blocks in the pipe and flush them.
		 * 5. Activate the signpost if data is delivered and new space is vacated.
		 * 6. Activate the signpost if space is already available.
		 * 7. Deactivate the signpost otherwise.
		 * 8. Calculate the new hardware pointer and return it.
		 */
		snd_pcm_uframes_t pointer(ALSA::IOPlug& ioplug)
		{
			const snd_pcm_uframes_t period = ioplug.period_size();
			const snd_pcm_uframes_t used = ioplug.buffer_used();
			const snd_pcm_uframes_t free = ioplug.buffer_free();

			m_timer.try_accumulate(0);

			const unsigned int available = used / period;
			const unsigned int outstanding = m_timer.consume();

			if (outstanding > available) {
				m_timer.stop();
				m_signpost.deactivate();
				throw ALSA::XrunException();
			}

			const Piper::Inlet::Position start = m_inlet.start();
			const Piper::Inlet::Position until = start + outstanding;

			for (Piper::Inlet::Position position = start; position < until; position++) {
				m_inlet.preamble(position).timestamp = Piper::now();
				m_inlet.flush();
			}

			if (outstanding > 0) {
				m_signpost.activate();
			} else if (free > 0) {
				m_signpost.activate();
			} else {
				m_signpost.deactivate();
			}

			const snd_pcm_uframes_t boundary = ioplug.boundary();
			const snd_pcm_uframes_t current_pointer = ioplug.hardware_pointer();
			const snd_pcm_uframes_t next_pointer = (current_pointer + outstanding * period) % boundary;
			return next_pointer;
		}

		/**
		 * Transfer data into the writable block of the pipe.
		 */
		snd_pcm_uframes_t transfer(ALSA::IOPlug& ioplug, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
		{
			assert(areas != nullptr);
			assert(size > 0);

			const snd_pcm_uframes_t period = ioplug.period_size();
			const snd_pcm_uframes_t used = ioplug.buffer_used();
			const snd_pcm_uframes_t free = ioplug.buffer_free();

			Piper::Inlet::Position increment = used / period;
			Piper::Inlet::Position block = m_inlet.start() + increment;
			Piper::Buffer buffer = m_inlet.content(block);
			snd_pcm_uframes_t pending = size;
			snd_pcm_uframes_t done = 0;

			m_transfer_source.reset(areas, offset + size);
			m_transfer_target.reset(buffer.start(), buffer.size());
			m_transfer_source.behead(offset);
			m_transfer_target.behead(used - increment * period);

			if (pending > free) {
				SNDERR("device cannot be written: insufficient space (%lu) for incoming data (%lu)", free, size);
				pending = free;
			}

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

			if (free == 0) {
				m_signpost.deactivate();
			} else if (free == done) {
				m_signpost.deactivate();
			} else {
				m_signpost.activate();
			}

			return done;
		}

	private:

		Piper::Pipe m_pipe;
		Piper::Inlet m_inlet;
		Piper::Timer m_timer;
		Piper::SignPost m_signpost;
		ALSA::Range m_transfer_source;
		ALSA::Range m_transfer_target;

};


/**
 * This class handles the callbacks of piper plugin over capture stream.
 */
class PiperCaptureHandler : public ALSA::IOPlug::Handler
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
			m_transfer_source(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_transfer_target(m_pipe.format_code_alsa(), m_pipe.channels()),
			m_cursor(m_outlet.until())
		{
			// do nothing
		}

		/**
		 * Configure the capture device before its creation.
		 */
		void configure(const char* name, snd_pcm_stream_t stream, int mode, ALSA::IOPlug::Options& options)
		{
			options.name = name;
			options.enable_prepare_callback = true;
			options.enable_poll_descriptors_count_callback = true;
			options.enable_poll_descriptors_callback = true;
			options.enable_poll_revents_callback = true;
			options.enable_transfer_callback = true;
		}

		/**
		 * Limits the hardware parameter the capture device accepts.
		 */
		void create(ALSA::IOPlug& ioplug)
		{
			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(m_pipe.format_code_alsa()) };
			unsigned int channels_list[] = { m_pipe.channels() };
			unsigned int rate_list[] = { m_pipe.rate() };
			unsigned int period_list[] = { static_cast<unsigned int>(m_pipe.period_size()) };

			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_ACCESS, 2, access_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_FORMAT, 1, format_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_RATE, 1, rate_list);
			ioplug.set_parameter_list(SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list);
			ioplug.set_parameter_range(SND_PCM_IOPLUG_HW_PERIODS, 2, m_outlet.window());
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
		void prepare(ALSA::IOPlug& ioplug)
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
		void start(ALSA::IOPlug& ioplug)
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
		void stop(ALSA::IOPlug& ioplug)
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
		int poll_descriptors_count(ALSA::IOPlug& ioplug)
		{
			return 2;
		}

		/**
		 * Return the details of descriptors that should be polled by the client
		 * for device events. See the other poll callbacks for more information.
		 */
		int poll_descriptors(ALSA::IOPlug& ioplug, struct pollfd* pfd, unsigned int space)
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
		void poll_revents(ALSA::IOPlug& ioplug, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
		{
			assert(pfd != nullptr);
			assert(revents != nullptr);
			assert(nfds >= 2);

			for (unsigned int i = 0; i < nfds; i++) {
				if (pfd[i].revents != 0) {
					*revents = pfd[i].revents;
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
		snd_pcm_uframes_t pointer(ALSA::IOPlug& ioplug)
		{
			const snd_pcm_uframes_t period = ioplug.period_size();
			const snd_pcm_uframes_t used = ioplug.buffer_used();
			const snd_pcm_uframes_t free = ioplug.buffer_free();

			m_timer.try_accumulate(0);
			m_timer.consume();

			const Piper::Outlet::Position until = m_outlet.until();
			const Piper::Outlet::Position delta = until - m_cursor;
			const snd_pcm_uframes_t incoming = period * delta;

			if (incoming > free) {
				m_timer.stop();
				m_signpost.deactivate();
				throw ALSA::XrunException();
			}

			const snd_pcm_uframes_t boundary = ioplug.boundary();
			const snd_pcm_uframes_t current_pointer = ioplug.hardware_pointer();
			const snd_pcm_uframes_t next_pointer = (current_pointer + incoming) % boundary;

			if (incoming > 0) {
				m_cursor = until;
				m_signpost.activate();
			} else if (used > 0) {
				m_signpost.activate();
			} else {
				m_signpost.deactivate();
			}

			return next_pointer;
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
		snd_pcm_uframes_t transfer(ALSA::IOPlug& ioplug, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
		{
			assert(areas != nullptr);
			assert(size > 0);

			const snd_pcm_uframes_t period = ioplug.period_size();
			const snd_pcm_uframes_t used = ioplug.buffer_used();
			
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

		Piper::Pipe m_pipe;
		Piper::Outlet m_outlet;
		Piper::Timer m_timer;
		Piper::SignPost m_signpost;
		ALSA::Range m_transfer_source;
		ALSA::Range m_transfer_target;
		Piper::Outlet::Position m_cursor;

};


extern "C"
{

	/**
	 * Open the device. The function will parse the device configuration and call
	 * the appropriate open function to initialize the device.
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
				std::unique_ptr<ALSA::IOPlug::Handler> handler(new PiperPlaybackHandler(path));
				*pcmp = ALSA::IOPlug::open(name, stream, mode, std::move(handler));
				return 0;
			} else if (stream == SND_PCM_STREAM_CAPTURE) {
				std::unique_ptr<ALSA::IOPlug::Handler> handler(new PiperCaptureHandler(path));
				*pcmp = ALSA::IOPlug::open(name, stream, mode, std::move(handler));
				return 0;
			} else {
				SNDERR("device %s cannot be initialized: device supports playback only", name);
				return -EINVAL;
			}
		} catch (Piper::Exception& ex) {
			SNDERR("device %s cannot be opened: %s from file %s line %d", name, ex.what(), ex.file(), ex.line());
			return -EIO;
		} catch (std::system_error& ex) {
			int code = ex.code().value();
			SNDERR("device %s cannot be opened: %s", name, ex.what());
			return (code < 0 ? code : -code);
		} catch (std::bad_alloc& ex) {
			SNDERR("device %s cannot be opened: memory error");
			return -ENOMEM;
		} catch (std::invalid_argument& ex) {
			SNDERR("device %s cannot be opened: invalid argument");
			return -EINVAL;
		} catch (...) {
			SNDERR("device %s cannot be opened: unknown error");
			return -EIO;
		}
	}

	SND_PCM_PLUGIN_SYMBOL(piper);

}


