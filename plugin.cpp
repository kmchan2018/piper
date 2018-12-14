

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


#define DPRINTF(s, ...) fprintf(stderr, (s), __VA_ARGS__)
//#define DPRINTF(s, ...)


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
	static snd_pcm_uframes_t difference(snd_pcm_uframes_t start, snd_pcm_uframes_t end, snd_pcm_uframes_t wraparound)
	{
		if (start <= end) {
			return end - start;
		} else {
			return (wraparound - end) + start;
		}
	}

	/**
	 * Query software parameters of the playback plugin. It fetches the boundary
	 * parameter so that we can handle pointer wrap around properly.
	 */
	static int piper_playback_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		DPRINTF("[DEBUG] querying software parameters for device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);

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
	 * Prepare the playback in the playback plugin. It starts the internal
	 * timer that controls the buffer consumption.
	 */
	static int piper_playback_prepare(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] preparing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			plugin->timer->stop();
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
	 * Start the playback in the playback plugin. It starts (or restarts) the
	 * internal timer that controls buffer consumption.
	 */
	static int piper_playback_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			plugin->timer->stop();
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
	 * Stop the playback in the playback plugin. It stops the internal timer
	 * that controls buffer consumption.
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
	 * Return the number of descriptors that should be monitored.
	 */
	static int piper_playback_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		return 2;
	}

	/**
	 * Return the descriptors that should be monitored.
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
	 * Demangle the poll result. Application monitors the timer descriptor
	 * for write opportunities, but that descriptor can only be polled for
	 * read opportunities. This callback patches the poll result.
	 * .
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
	 * Update the hardware pointer of the playback plugin. It checks the timer
	 * for overdue periods in the buffer and drains them into the pipe.
	 */
	static snd_pcm_sframes_t piper_playback_pointer(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] draining device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			Piper::Inlet* inlet = plugin->inlet.get();
			Piper::Timer* timer = plugin->timer.get();
			Piper::SignPost* signpost = plugin->signpost.get();

			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t occupied = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);

			timer->try_accumulate(0);

			Piper::Inlet::Position position = inlet->start();
			unsigned int available = occupied / period;
			unsigned int outstanding = timer->consume();

			if (outstanding > available) {
				SNDERR("device %s cannot be drained: insufficient data to flush\n", ioplug->name);
				timer->stop();
				signpost->deactivate();
				return -EPIPE;
			}

			for (unsigned int i = 0; i < outstanding; i++) {
				inlet->preamble(position++).timestamp = Piper::now();
				inlet->flush();
			}

			if (outstanding > 0) {
				signpost->activate();
			}

			snd_pcm_uframes_t start = ioplug->hw_ptr % capacity;
			snd_pcm_uframes_t copied = outstanding * period;
			return (start + copied) % capacity;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be drained: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be drained: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Transfer data into the buffer of the playback plugin. It will copy data
	 * from the input into the appropriate writable blocks in the pipe.
	 */
	static snd_pcm_sframes_t piper_playback_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *input_areas, snd_pcm_uframes_t input_start, snd_pcm_uframes_t input_size)
	{
		DPRINTF("[DEBUG] writing device %s\n", ioplug->name);

		try {
			PiperPlaybackPlugin* plugin = static_cast<PiperPlaybackPlugin*>(ioplug->private_data);
			Piper::Inlet* inlet = plugin->inlet.get();
			Piper::SignPost* signpost = plugin->signpost.get();

			const unsigned int channels = ioplug->channels;
			const snd_pcm_format_t format = ioplug->format;
			const snd_pcm_uframes_t buffer_capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t buffer_occupied = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);
			const snd_pcm_uframes_t buffer_writable = buffer_capacity - buffer_occupied;
			const snd_pcm_uframes_t target_capacity = ioplug->period_size;

			if (input_size > buffer_writable) {
				SNDERR("device %s cannot be written: insufficient space (%lu) for incoming data (%lu)", buffer_occupied, input_size);
				input_size = buffer_writable;
			}

			Piper::Inlet::Position target_position = inlet->start() + (buffer_occupied / target_capacity);
			snd_pcm_channel_area_t* target_areas = plugin->areas.data();
			snd_pcm_uframes_t target_start = buffer_occupied % target_capacity;
			snd_pcm_uframes_t target_size = std::min(input_size, (target_start == 0 ? target_capacity : target_capacity - target_start));
			snd_pcm_uframes_t copied = 0;

			while (input_size > 0) {
				char* pointer = inlet->content(target_position).start();
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
				target_size = std::min(input_size, target_capacity);
			}

			if (copied == buffer_writable) {
				signpost->deactivate();
			} else {
				signpost->activate();
			}

			return static_cast<snd_pcm_sframes_t>(copied);

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be written: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be written: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Close the playback plugin. It will release any resource associated with
	 * the plugin.
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
	 * Open the playback device. It will allocate resources for the playback
	 * plugin as well as configuring it.
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

			for (unsigned int i = 0; i < plugin->pipe->channels(); i++) {
				plugin->areas[i].addr = nullptr;
				plugin->areas[i].first = snd_pcm_format_physical_width(plugin->pipe->format()) * i;
				plugin->areas[i].step = 8 * plugin->pipe->frame_size();
			}

			unsigned int accesses[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format = static_cast<unsigned int>(plugin->pipe->format());
			unsigned int channels = static_cast<unsigned int>(plugin->pipe->channels());
			unsigned int rate = static_cast<unsigned int>(plugin->pipe->rate());
			unsigned int period = static_cast<unsigned int>(plugin->pipe->period_size());
			int err = 0;

			if ((err = snd_pcm_ioplug_create(&plugin->io, name, stream, mode)) < 0) {
				SNDERR("device %s cannot be initialized: snd_pcm_ioplug_create fail to complete", name, err);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_ACCESS, 2, accesses)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_ACCESS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_FORMAT, 1, &format)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_FORMAT", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, &channels)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_CHANNELS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_RATE, 1, &rate)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_RATE", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, &period)) < 0) {
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
	 * Check software parameters of the capture plugin. It fetches the boundary
	 * parameter so that we can handle pointer wrap around properly.
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
	 * Prepare the capture plugin. It signals that the device is readable.
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
	 * Start the capture in the capture plugin. It starts (or restarts) the
	 * internal timer that controls buffer supplement.
	 */
	static int piper_capture_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			plugin->timer->stop();
			plugin->timer->start();
			plugin->signpost->deactivate();
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
	 * Stop the capture in the capture plugin. It stops the internal timer
	 * that controls buffer supplement.
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
	 * Return the number of descriptors that should be monitored.
	 */
	static int piper_capture_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		return 2;
	}

	/**
	 * Return the descriptors that should be monitored.
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
	 * Demangle the poll result. Application monitors the timer descriptor
	 * for read opportunities, but that descriptor can only be polled for
	 * read opportunities. This callback patches the poll result.
	 * .
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
	 * Update the hardware pointer of the capture plugin. It checks the timer
	 * for incoming periods in the pipe and fill them into the buffer.
	 */
	static snd_pcm_sframes_t piper_capture_pointer(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] filling device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			Piper::Outlet* outlet = plugin->outlet.get();
			Piper::Timer* timer = plugin->timer.get();
			Piper::SignPost* signpost = plugin->signpost.get();

			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t readable = difference(ioplug->appl_ptr, ioplug->hw_ptr, plugin->boundary);
			const snd_pcm_uframes_t fillable = capacity - readable;

			timer->try_accumulate(0);
			timer->consume();

			Piper::Inlet::Position until = outlet->until();
			snd_pcm_uframes_t incoming = period * (until - plugin->cursor);

			if (incoming > fillable) {
				SNDERR("device %s cannot be filled: insufficient space to fill\n", ioplug->name);
				return -EPIPE;
			} else if (incoming > 0) {
				signpost->activate();
				plugin->cursor = until;
				return (ioplug->hw_ptr + incoming) % capacity;
			} else if (readable > 0) {
				signpost->activate();
				return ioplug->hw_ptr % capacity;
			} else {
				signpost->deactivate();
				return ioplug->hw_ptr % capacity;
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
	 * Transfer data into the buffer of the capture plugin. It will copy data
	 * from the readable blocks in the pipe to the destination.
	 */
	static snd_pcm_sframes_t piper_capture_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *output_areas, snd_pcm_uframes_t output_start, snd_pcm_uframes_t output_size)
	{
		DPRINTF("[DEBUG] reading device %s\n", ioplug->name);

		try {
			PiperCapturePlugin* plugin = static_cast<PiperCapturePlugin*>(ioplug->private_data);
			Piper::Outlet* outlet = plugin->outlet.get();

			const unsigned int channels = ioplug->channels;
			const snd_pcm_format_t format = ioplug->format;
			const snd_pcm_uframes_t buffer_readable = difference(ioplug->appl_ptr, ioplug->hw_ptr, plugin->boundary);
			const snd_pcm_uframes_t source_capacity = ioplug->period_size;

			if (output_size > buffer_readable) {
				SNDERR("device %s cannot be read: insufficient data (%lu) than requested (%lu)", buffer_readable, output_size);
				output_size = buffer_readable;
			}

			Piper::Inlet::Position source_position = outlet->until() - (buffer_readable / source_capacity) - (buffer_readable % source_capacity > 0 ? 1 : 0);
			snd_pcm_channel_area_t* source_areas = plugin->areas.data();
			snd_pcm_uframes_t source_start = ioplug->appl_ptr % source_capacity;
			snd_pcm_uframes_t source_size = std::min(output_size, (source_start == 0 ? source_capacity : source_capacity - source_start));
			snd_pcm_uframes_t copied = 0;

			while (output_size > 0) {
				const char* pointer = outlet->content(source_position).start();
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
				source_size = std::min(output_size, source_capacity);
			}

			if (copied == buffer_readable) {
				plugin->signpost->deactivate();
			} else {
				plugin->signpost->activate();
			}

			return static_cast<snd_pcm_sframes_t>(copied);

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be read: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be read: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Close the capture plugin. It will release any resource associated with
	 * the plugin.
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
	 * Open the playback device. It will allocate resources for the playback
	 * plugin as well as configuring it.
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

			for (unsigned int i = 0; i < plugin->pipe->channels(); i++) {
				plugin->areas[i].addr = nullptr;
				plugin->areas[i].first = snd_pcm_format_physical_width(plugin->pipe->format()) * i;
				plugin->areas[i].step = 8 * plugin->pipe->frame_size();
			}

			unsigned int accesses[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED };
			unsigned int format = static_cast<unsigned int>(plugin->pipe->format());
			unsigned int channels = static_cast<unsigned int>(plugin->pipe->channels());
			unsigned int rate = static_cast<unsigned int>(plugin->pipe->rate());
			unsigned int period = static_cast<unsigned int>(plugin->pipe->period_size());
			int err = 0;

			if ((err = snd_pcm_ioplug_create(&plugin->io, name, stream, mode)) < 0) {
				SNDERR("device %s cannot be initialized: snd_pcm_ioplug_create fail to complete", name, err);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_ACCESS, 2, accesses)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_ACCESS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_FORMAT, 1, &format)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_FORMAT", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, &channels)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_CHANNELS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_RATE, 1, &rate)) < 0) {
				SNDERR("device %s cannot be initialized: cannot configure hardware parameter SND_PCM_IOPLUG_HW_RATE", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, &period)) < 0) {
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
	 * Open the playback device. It will allocate resources for the plugin as
	 * well as configuring it.
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


