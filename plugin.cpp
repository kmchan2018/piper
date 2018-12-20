

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/pcm_ioplug.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "pipe.hpp"
#include "signpost.hpp"
#include "timer.hpp"


//#define DPRINTF(s, ...) fprintf(stderr, (s), __VA_ARGS__)
#define DPRINTF(s, ...)


/**
 * Playback plugin.
 */
struct PiperPlaybackPlugin
{
	snd_pcm_ioplug_t io;
	snd_pcm_ioplug_callback_t callback;
	snd_pcm_uframes_t boundary;
	std::string name;
	std::vector<snd_pcm_channel_area_t> areas;
	std::unique_ptr<Piper::Pipe> pipe;
	std::unique_ptr<Piper::Inlet> inlet;
	std::unique_ptr<Piper::Timer> timer;
	std::unique_ptr<Piper::SignPost> signpost;
};

/**
 * Capture plugin.
 */
struct PiperCapturePlugin
{
	snd_pcm_ioplug_t io;
	snd_pcm_ioplug_callback_t callback;
	snd_pcm_uframes_t boundary;
	Piper::Inlet::Position cursor;
	std::string name;
	std::vector<snd_pcm_channel_area_t> areas;
	std::unique_ptr<Piper::Pipe> pipe;
	std::unique_ptr<Piper::Outlet> outlet;
	std::unique_ptr<Piper::Timer> timer;
	std::unique_ptr<Piper::SignPost> signpost;
};

extern "C"
{

	/**
	 * Calculate the difference between 2 pointers with wrap around.
	 */
	static inline snd_pcm_uframes_t difference(snd_pcm_uframes_t start, snd_pcm_uframes_t end, snd_pcm_uframes_t wraparound)
	{
		if (start <= end) {
			return end - start;
		} else {
			return (wraparound - end) + start;
		}
	}

	/**
	 * Query software parameters of the playback device.
	 *
	 * This callback will fetch the boundary parameter and save it so that other
	 * callbacks can handle pointer wrap-around properly.
	 *
	 * The period event parameter is currently ignored. Due to the manner the 
	 * plugin updates the hardware pointer, the device will always run as if the
	 * period event parameter is activated.
	 *
	 * The avail min parameter is also currently ignored. The device will run as
	 * if the parameter is set to 1.
	 */
	static int piper_playback_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		DPRINTF("[DEBUG] querying software parameters for device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			int err = 0;

			if ((err = snd_pcm_sw_params_get_boundary(params, &plugin->boundary)) < 0) {
				SNDERR("device %s cannot be prepared: cannot fetch boundary from sofware parameters\n", ioplug->name);
				return err;
			} else {
				return 0;
			}
		} catch (std::exception& ex) {
			SNDERR("device %s cannot be queried: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be queried: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Prepare the playback device. After this call, the device will enter
	 * PREPARED state. At this point, the device should have an empty "buffer"
	 * and hence it should be able to receive audio data from the application.
	 *
 	 * This callback will activate the signpost to report that the device can
	 * be written to.
	 */
	static int piper_playback_prepare(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] preparing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			plugin->signpost->activate();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be started: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be started: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Start the playback device. After this call, the device will enter RUNNING
	 * state. In this state, the device should periodically deliver audio data
	 * in the device "buffer" into the pipe.
	 *
	 * This callback will start the timer to signal possible hardware pointer
	 * updates every period. The application will then handle the signals and
	 * deliver audio data via the pointer callback.
	 */
	static int piper_playback_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			plugin->timer->start();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be started: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be started: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Stop the playback in the playback device. After this call, the device will
	 * return to SETUP state, and should stop accepting audio data nor delivering
	 * them into the pipe.
	 *
	 * This callback will stop the timer and deactivate the signpost to reflect
	 * the situation.
	 */
	static int piper_playback_stop(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] stopping device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			plugin->timer->stop();
			plugin->signpost->deactivate();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be stopped due to %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be stopped due to unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Return the number of descriptors that should be polled by the application
	 * for device events.
	 *
	 * This callback will return 2 for descriptors from both timer and signpost.
	 * The timer descriptor is responsible for signalling each lapse of period
	 * and possible hardware pointer changes; on the other hand, the signpost
	 * descriptor is responsible for advertising availability of space in the
	 * device "buffer" and hence opportunities of non-blocking writes.
	 */
	static int piper_playback_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		return 2;
	}

	/**
	 * Return the details of descriptors that should be polled by the application
	 * for device events. See the other poll callbacks for more information.
	 */
	static int piper_playback_poll_descriptors(snd_pcm_ioplug_t* ioplug, struct pollfd* pfd, unsigned int space)
	{
		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);

			if (space >= 2) {
				pfd[0].fd = plugin->timer->descriptor();
				pfd[0].events = POLLIN;
				pfd[0].revents = 0;
				pfd[1].fd = plugin->signpost->descriptor();
				pfd[1].events = POLLIN;
				pfd[1].revents = 0;
				return 2;
			} else {
				return -EINVAL;
			}
		} catch (std::exception& ex) {
			SNDERR("device %s cannot be stopped due to %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be stopped due to unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Check the poll result and return the device events. Note that the callback
	 * will demangle the event code and translate POLLIN events to POLLOUT events.
	 * It is because both timer and signpost descriptors can only be polled for
	 * POLLIN events but the application expects POLLOUT events.
	 */
	static int piper_playback_poll_revents(snd_pcm_ioplug_t* ioplug, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
	{
		for (unsigned int i = 0; i < nfds; i++) {
			unsigned short temp = pfd[i].revents;

			if (temp != 0 && (temp & POLLIN) != 0) {
				*revents = (temp & ~POLLIN) | POLLOUT;
				return 0;
			} else if (temp != 0) {
				*revents = temp;
				return 0;
			}
		}

		return 0;
	}

	/**
	 * Return the hardware pointer of the playback device. It is called when the
	 * device is in PREPARED or RUNNING state to check the position of hardware
	 * pointer.
	 *
	 * This callback will deliver audio data into the pipe and return back the
	 * updated hardware pointer. It will also detect possible xrun and handle it
	 * as well. The process involves:
	 *
	 * 1. Check the device for amount of data available for delivery.
	 * 2. Check the timer for amount of data that should be delivered.
	 * 3. Report buffer underrun if insufficient data is available for delivery.
	 * 4. Timestamp of relevant writable blocks in the pipe and flush them.
	 * 5. Activate the signpost if data is delivered and new space is vacated.
	 * 6. Activate the signpost if data is not delivered but space is available.
	 * 7. Deactivate the signpost otherwise.
	 * 8. Calculate the new hardware pointer and return it.
	 */
	static snd_pcm_sframes_t piper_playback_pointer(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] flushing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);

			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t flushable = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);
			const snd_pcm_uframes_t writable = capacity - flushable;

			plugin->timer->try_accumulate(0);

			const unsigned int available = flushable / period;
			const unsigned int outstanding = plugin->timer->consume();

			if (outstanding > available) {
				SNDERR("device %s cannot be flushed: insufficient data to flush\n", ioplug->name);
				plugin->timer->stop();
				plugin->signpost->deactivate();
				return -EPIPE;
			}

			const snd_pcm_uframes_t copied = outstanding * period;
			const snd_pcm_uframes_t current = ioplug->hw_ptr % capacity;
			const snd_pcm_uframes_t next = (current + copied) % capacity;

			const Piper::Inlet::Position start = plugin->inlet->start();
			const Piper::Inlet::Position until = start + outstanding;

			for (Piper::Inlet::Position position = start; position < until; position++) {
				plugin->inlet->preamble(position).timestamp = Piper::now();
				plugin->inlet->flush();
			}

			if (outstanding > 0) {
				plugin->signpost->activate();
			} else if (writable > 0) {
				plugin->signpost->activate();
			} else {
				plugin->signpost->deactivate();
			}

			return next;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be flushed: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be drained: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Transfer data into the "buffer" of the playback device. It is called when
	 * the device is in PREPARED or RUNNING state in response to writes from the
	 * application.
	 *
	 * The callback will copy the written data to the device "buffer", aka the
	 * staging block of the pipe. The process involves:
	 *
	 * 1. Calculate the amount of space occupied by written data.
	 * 2. Calculate the amount of space to be occupied.
	 * 3. Limit the input data size to the amount calculated in step 2.
	 * 4. Retrieve the position of first staging block in the pipe which aligns
	 *    with the current hardware pointer. Then, use the amount of written
	 *    data calculated in step 1 to determine the first block which should
	 *    receive the data.
	 * 5. Determine the offset of the block that should receive the data.
	 * 6. Copy the incoming data to the block, moving to new blocks if necessary.
	 * 7. Deactivate the signpost if all space is occupied from the beginning. 
	 * 8. Deactivate the signpost if all space is occupied by the copy.
	 * 9. Activate the signpost otherwise.
	 * 10. Return the amount of frames copied.
	 */
	static snd_pcm_sframes_t piper_playback_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *input_areas, snd_pcm_uframes_t input_start, snd_pcm_uframes_t input_size)
	{
		DPRINTF("[DEBUG] writing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);

			const unsigned int channels = ioplug->channels;
			const snd_pcm_format_t format = ioplug->format;
			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t flushable = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);
			const snd_pcm_uframes_t writable = capacity - flushable;

			if (input_size > writable) {
				SNDERR("device %s cannot be written: insufficient space (%lu) for incoming data (%lu)", writable, input_size);
				input_size = writable;
			}

			Piper::Inlet::Position target_position = plugin->inlet->start() + flushable / period;
			snd_pcm_channel_area_t* target_areas = plugin->areas.data();
			snd_pcm_uframes_t target_start = flushable % period;
			snd_pcm_uframes_t target_size = std::min(input_size, period - target_start);
			snd_pcm_uframes_t copied = 0;

			while (input_size > 0) {
				char* pointer = plugin->inlet->content(target_position).start();
				int err;

				for (unsigned int i = 0; i < channels; i++) {
					target_areas[i].addr = pointer;
				}

				if ((err = snd_pcm_areas_copy(target_areas, target_start, input_areas, input_start, channels, target_size, format)) < 0) {
					SNDERR("device %s cannot be written: copy error", ioplug->name);
					return err;
				}

				copied += target_size;
				input_start += target_size;
				input_size -= target_size;
				target_position += 1;
				target_start = 0;
				target_size = std::min(input_size, period);
			}

			if (writable == 0) {
				plugin->signpost->deactivate();
			} else if (writable == copied) {
				plugin->signpost->deactivate();
			} else {
				plugin->signpost->activate();
			}

			return copied;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be written: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be written: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Close the playback device. The callback will release any resources used
	 * by the device.
	 */
	static int piper_playback_close(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] closing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			delete plugin;
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be closed: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be closed: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Open the playback device. The function will acquire resources for the
	 * device as well as configuring it. The process involves:
	 *
	 * 1. Create the pipe from the given file.
	 * 2. Create the inlet from the pipe.
	 * 3. Create the timer which triggers at every period.
	 * 4. Create the signpost.
	 * 5. Initialize the plugin options and callbacks.
	 * 6. Create the ioplug handle.
	 * 7. Restrict the hardware parameters accepted by the ioplug handle.
	 */
	static int piper_playback_open(snd_pcm_t** pcmp, const char* name, const char* path, snd_pcm_stream_t stream, int mode)
	{
		try {
			std::unique_ptr<PiperPlaybackPlugin> plugin(new PiperPlaybackPlugin);

			plugin->pipe.reset(new Piper::Pipe(path));
			plugin->inlet.reset(new Piper::Inlet(plugin->pipe.get()));
			plugin->timer.reset(new Piper::Timer(plugin->pipe->period_time()));
			plugin->signpost.reset(new Piper::SignPost());
			plugin->areas.resize(plugin->pipe->channels());

			plugin->name = name;
			plugin->boundary = 0;

			plugin->io.version = SND_PCM_IOPLUG_VERSION;
			plugin->io.name = plugin->name.data();
			plugin->io.poll_fd = plugin->timer->descriptor();
			plugin->io.poll_events = POLLIN;
			plugin->io.mmap_rw = 0;
			plugin->io.callback = &plugin->callback;
			plugin->io.private_data = plugin.get();

			memset(&plugin->callback, 0, sizeof(plugin->callback));
			plugin->callback.sw_params = piper_playback_sw_params;
			plugin->callback.prepare = piper_playback_prepare;
			plugin->callback.start = piper_playback_start;
			plugin->callback.stop = piper_playback_stop;
			plugin->callback.poll_descriptors_count = piper_playback_poll_descriptors_count;
			plugin->callback.poll_descriptors = piper_playback_poll_descriptors;
			plugin->callback.poll_revents = piper_playback_poll_revents;
			plugin->callback.pointer = piper_playback_pointer;
			plugin->callback.transfer = piper_playback_transfer;
			plugin->callback.close = piper_playback_close;

			snd_pcm_format_t format = plugin->pipe->format_code_alsa();
			unsigned int channels = plugin->pipe->channels();
			unsigned int rate = plugin->pipe->rate();
			std::size_t frame_size = plugin->pipe->frame_size();
			std::size_t period_size = plugin->pipe->period_size();

			for (unsigned int i = 0; i < channels; i++) {
				plugin->areas[i].addr = nullptr;
				plugin->areas[i].first = snd_pcm_format_physical_width(format) * i;
				plugin->areas[i].step = 8 * frame_size;
			}

			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(format) };
			unsigned int channels_list[] = { channels };
			unsigned int rate_list[] = { rate };
			unsigned int period_list[] = { static_cast<unsigned int>(period_size) };
			int err = 0;

			if ((err = snd_pcm_ioplug_create(&plugin->io, name, stream, mode)) < 0) {
				SNDERR("device %s cannot be initialized: snd_pcm_ioplug_create fail to complete", name, err);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_ACCESS, 2, access_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_ACCESS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_FORMAT, 1, format_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_FORMAT", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_CHANNELS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_RATE, 1, rate_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_RATE", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIOD_BYTES", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_minmax(&plugin->io, SND_PCM_IOPLUG_HW_PERIODS, 2, plugin->inlet->window())) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIODS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			}

			*pcmp = plugin->io.pcm;
			plugin.release();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be initialized: %s", name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be initialized: unknown exception", name);
			return -EBADFD;
		}
	}

	/**
	 * Query software parameters of the playback device.
	 *
	 * This callback will fetch the boundary parameter and save it so that other
	 * callbacks can handle pointer wrap-around properly.
	 *
	 * The period event parameter is currently ignored. Due to the manner the 
	 * plugin updates the hardware pointer, the device will always run as if the
	 * period event parameter is activated.
	 *
	 * The avail min parameter is also currently ignored. The device will run as
	 * if the parameter is set to 1.
	 */
	static int piper_capture_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		DPRINTF("[DEBUG] querying software parameters for device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);

			if (snd_pcm_sw_params_get_boundary(params, &plugin->boundary) < 0) {
				SNDERR("device %s cannot be prepared: cannot fetch boundary from sofware parameters\n", ioplug->name);
				return -EBADFD;
			}

			DPRINTF("[DEBUG] device %s has a boundary of %lu\n", ioplug->name, plugin->boundary);
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be queried: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be queried: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Prepare the capture device. After this call, the device will enter the
	 * PREPARED state. At this point, the device should have an empty "buffer".
	 * Note that unlike playback device, it means that no audio data can be read
	 * from the device.
	 *
 	 * This callback will deactivate the signpost to report that the device cannot
	 * be read from yet.
	 */
	static int piper_capture_prepare(snd_pcm_ioplug_t* ioplug)
	{
		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			plugin->signpost->deactivate();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be started: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be started: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Start the capture device. After this call, the device will enter the
	 * RUNNING state. In this state, the device should periodically deliver
	 * audio data from the pipe to the device "buffer" for reading.
	 *
	 * This callback will start the timer to signal possible data arrival and
	 * hardware pointer updates. It will also initialize the cursor to point
	 * to the end of pipe read window.
	 */
	static int piper_capture_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			plugin->timer->start();
			plugin->cursor = plugin->outlet->until();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be started: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be started: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Stop the capture device. After this call, the device will enter the SETUP
	 * state. In this state, the device will no longer deliver audio data from
	 * the pipe to the device "buffer" and then to the application.
	 *
	 * The callback will stop the timer and deactivate the signpost to reflect
	 * the situation.
	 */
	static int piper_capture_stop(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] stopping device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			plugin->timer->stop();
			plugin->signpost->deactivate();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be stopped due to %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be stopped due to unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Return the number of descriptors that should be polled by the application
	 * for device events.
	 *
	 * The callback will return 2 for descriptors from both timer and signpost.
	 * The timer descriptor is responsible for signalling each lapse of period
	 * and possible hardware pointer changes; on the other hand, the signpost
	 * descriptor is responsible for advertising availability of data in the
	 * device "buffer" and hence opportunities of non-blocking reads.
	 */
	static int piper_capture_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		return 2;
	}

	/**
	 * Return the details of descriptors that should be polled by the application
	 * for device events. See the other poll callbacks for more information.
	 */
	static int piper_capture_poll_descriptors(snd_pcm_ioplug_t* ioplug, struct pollfd* pfd, unsigned int space)
	{
		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);

			if (space >= 2) {
				pfd[0].fd = plugin->timer->descriptor();
				pfd[0].events = POLLIN;
				pfd[0].revents = 0;
				pfd[1].fd = plugin->signpost->descriptor();
				pfd[1].events = POLLIN;
				pfd[1].revents = 0;
				return 2;
			} else {
				return -EINVAL;
			}

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be stopped due to %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be stopped due to unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Check the poll result and return the device events. Note that the callback
	 * will not need to mangle event codes.
	 */
	static int piper_capture_poll_revents(snd_pcm_ioplug_t* ioplug, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
	{
		for (unsigned int i = 0; i < nfds; i++) {
			if (pfd[i].revents != 0) {
				*revents = pfd[i].revents;
				return 0;
			}
		}

		return 0;
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
	static snd_pcm_sframes_t piper_capture_pointer(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] filling device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);

			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t readable = difference(ioplug->appl_ptr, ioplug->hw_ptr, plugin->boundary);
			const snd_pcm_uframes_t fillable = capacity - readable;

			plugin->timer->try_accumulate(0);
			plugin->timer->consume();

			const Piper::Inlet::Position until = plugin->outlet->until();
			const Piper::Inlet::Position delta = until - plugin->cursor;

			const snd_pcm_uframes_t incoming = period * delta;
			const snd_pcm_uframes_t current = ioplug->hw_ptr % capacity;
			const snd_pcm_uframes_t next = (current + incoming) % capacity;

			if (incoming > fillable) {
				SNDERR("device %s cannot be filled: insufficient space to fill\n", ioplug->name);
				return -EPIPE;
			} else if (incoming > 0) {
				plugin->signpost->activate();
				plugin->cursor = until;
				return next;
			} else if (readable > 0) {
				plugin->signpost->activate();
				return next;
			} else {
				plugin->signpost->deactivate();
				return next;
			}
		} catch (std::exception& ex) {
			SNDERR("device %s cannot be filled: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be filled: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Transfer data out of the "buffer" of the playback device. It is called
	 * when the device is in RUNNING state in response to reads from the
	 * application.
	 *
	 * This callback will copy existing audio data from the device "buffer" to
	 * the application buffer. The process involves:
	 *
	 * 1. Calculate the amount of readable data in the device "buffer".
	 * 2. Limit the output size to the amount of readable data.
	 * 3. Calculate the position of first block where readable data is located.
	 * 4. Calculate the offset in the first block where readable data is located.
	 * 5. Copy audio data to the destination, moving to new blocks as necessary.
	 * 6. Deactivate the signpost if no data is readable from the start.
	 * 6. Deactivate the signpost if no data is readable after the copy.
	 * 7. Activate the signpost otherwise.
	 * 8. Return the amount of frames copied.
	 */
	static snd_pcm_sframes_t piper_capture_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *output_areas, snd_pcm_uframes_t output_start, snd_pcm_uframes_t output_size)
	{
		DPRINTF("[DEBUG] reading device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);

			const unsigned int channels = ioplug->channels;
			const snd_pcm_format_t format = ioplug->format;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t readable = difference(ioplug->appl_ptr, ioplug->hw_ptr, plugin->boundary);

			if (output_size > readable) {
				SNDERR("device %s cannot be read: insufficient data (%lu) than requested (%lu)", readable, output_size);
				output_size = readable;
			}

			const snd_pcm_uframes_t readable_blocks = readable / period;
			const snd_pcm_uframes_t readable_leftovers = readable % period;

			Piper::Inlet::Position source_position = plugin->cursor - readable_blocks - (readable_leftovers > 0 ? 1 : 0);
			snd_pcm_channel_area_t* source_areas = plugin->areas.data();
			snd_pcm_uframes_t source_start = (readable_leftovers > 0 ? period - readable_leftovers : 0);
			snd_pcm_uframes_t source_size = (readable_leftovers > 0 ? readable_leftovers : period);
			snd_pcm_uframes_t copied = 0;

			while (output_size > 0) {
				const char* pointer = plugin->outlet->content(source_position).start();
				int err;

				for (unsigned int i = 0; i < channels; i++) {
					source_areas[i].addr = reinterpret_cast<void*>(const_cast<char*>(pointer));
				}

				if ((err = snd_pcm_areas_copy(output_areas, output_start, source_areas, source_start, channels, source_size, format)) < 0) {
					SNDERR("device %s cannot be read: copy error", ioplug->name);
					return err;
				}

				copied += source_size;
				output_start += source_size;
				output_size -= source_size;
				source_position += 1;
				source_start = 0;
				source_size = std::min(output_size, period);
			}

			if (readable == 0) {
				plugin->signpost->deactivate();
			} else if (readable == copied) {
				plugin->signpost->deactivate();
			} else {
				plugin->signpost->activate();
			}

			return copied;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be read: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be read: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Close the capture device. The callback will release any resources used
	 * by the device.
	 */
	static int piper_capture_close(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] closing device %s...\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			delete plugin;
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be closed: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be closed: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Open the capture device. The function will allocate resources for the
	 * device as well as configuring it. The process involves:
	 *
	 * 1. Create the pipe from the given file.
	 * 2. Create the outlet from the pipe.
	 * 3. Create the timer which triggers at every period.
	 * 4. Create the signpost.
	 * 5. Initialize the plugin options and callbacks.
	 * 6. Create the ioplug handle.
	 * 7. Restrict the hardware parameters accepted by the ioplug handle.
	 */
	static int piper_capture_open(snd_pcm_t** pcmp, const char* name, const char* path, snd_pcm_stream_t stream, int mode)
	{
		try {
			std::unique_ptr<PiperCapturePlugin> plugin(new PiperCapturePlugin);

			plugin->pipe.reset(new Piper::Pipe(path));
			plugin->outlet.reset(new Piper::Outlet(plugin->pipe.get()));
			plugin->timer.reset(new Piper::Timer(plugin->pipe->period_time()));
			plugin->signpost.reset(new Piper::SignPost());
			plugin->areas.resize(plugin->pipe->channels());

			plugin->name = name;
			plugin->cursor = plugin->outlet->until();
			plugin->boundary = 0;

			plugin->io.version = SND_PCM_IOPLUG_VERSION;
			plugin->io.name = plugin->name.data();
			plugin->io.poll_fd = plugin->timer->descriptor();
			plugin->io.poll_events = POLLIN;
			plugin->io.mmap_rw = 0;
			plugin->io.callback = &plugin->callback;
			plugin->io.private_data = plugin.get();

			memset(&plugin->callback, 0, sizeof(plugin->callback));
			plugin->callback.sw_params = piper_capture_sw_params;
			plugin->callback.prepare = piper_capture_prepare;
			plugin->callback.start = piper_capture_start;
			plugin->callback.stop = piper_capture_stop;
			plugin->callback.poll_descriptors_count = piper_capture_poll_descriptors_count;
			plugin->callback.poll_descriptors = piper_capture_poll_descriptors;
			plugin->callback.poll_revents = piper_capture_poll_revents;
			plugin->callback.pointer = piper_capture_pointer;
			plugin->callback.transfer = piper_capture_transfer;
			plugin->callback.close = piper_capture_close;

			snd_pcm_format_t format = plugin->pipe->format_code_alsa();
			unsigned int channels = plugin->pipe->channels();
			unsigned int rate = plugin->pipe->rate();
			std::size_t frame_size = plugin->pipe->frame_size();
			std::size_t period_size = plugin->pipe->period_size();

			for (unsigned int i = 0; i < channels; i++) {
				plugin->areas[i].addr = nullptr;
				plugin->areas[i].first = snd_pcm_format_physical_width(format) * i;
				plugin->areas[i].step = 8 * frame_size;
			}

			unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format_list[] = { static_cast<unsigned int>(format) };
			unsigned int channels_list[] = { channels };
			unsigned int rate_list[] = { rate };
			unsigned int period_list[] = { static_cast<unsigned int>(period_size) };
			int err = 0;

			if ((err = snd_pcm_ioplug_create(&plugin->io, name, stream, mode)) < 0) {
				SNDERR("device %s cannot be initialized: snd_pcm_ioplug_create fail to complete", name, err);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_ACCESS, 2, access_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_ACCESS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_FORMAT, 1, format_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_FORMAT", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, channels_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_CHANNELS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_RATE, 1, rate_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_RATE", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, period_list)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIOD_BYTES", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_minmax(&plugin->io, SND_PCM_IOPLUG_HW_PERIODS, 2, plugin->outlet->window())) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIODS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			}

			*pcmp = plugin->io.pcm;
			plugin.release();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be initialized: %s", name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be initialized: unknown exception", name);
			return -EBADFD;
		}
	}

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

		if (stream == SND_PCM_STREAM_PLAYBACK) {
			return piper_playback_open(pcmp, name, path, stream, mode);
		} else if (stream == SND_PCM_STREAM_CAPTURE) {
			return piper_capture_open(pcmp, name, path, stream, mode);
		} else {
			SNDERR("device %s cannot be initialized: device supports playback only", name);
			return -EINVAL;
		}
	}

	SND_PCM_PLUGIN_SYMBOL(piper);

}


