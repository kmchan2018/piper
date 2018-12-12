

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
#include "timer.hpp"


//#define DPRINTF(s, ...) fprintf(stderr, (s), __VA_ARGS__)
#define DPRINTF(s, ...)


/**
 * Playback plugin.
 */
struct PiperPlugin
{
	snd_pcm_ioplug_t io;
	snd_pcm_ioplug_callback_t callback;
	snd_pcm_uframes_t boundary;
	std::vector<snd_pcm_channel_area_t> areas;
	std::unique_ptr<Piper::Timer> timer;
	std::unique_ptr<Piper::Pipe> pipe;
	std::unique_ptr<Piper::Inlet> inlet;
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
	 * Check software parameters of the playback plugin. It fetches the boundary
	 * parameter so that we can handle pointer wrap around properly.
	 */
	static int piper_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);

		DPRINTF("[DEBUG] checking device parameters%s\n", ioplug->name);
		DPRINTF("[DEBUG] device %s has a boundary of %lu previously\n", ioplug->name, plugin->boundary);

		if (snd_pcm_sw_params_get_boundary(params, &plugin->boundary) < 0) {
			SNDERR("device %s cannot be prepared: cannot fetch boundary from sofware parameters\n", ioplug->name);
			return -EBADFD;
		} else {
			DPRINTF("[DEBUG] device %s has a boundary of %lu\n", ioplug->name, plugin->boundary);
			DPRINTF("[DEBUG] device %s is prepared\n", ioplug->name);
			return 0;
		}
	}

	/**
	 * Start the playback in the playback plugin. It starts (or restarts) the
	 * internal timer that controls buffer consumption.
	 */
	static int piper_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
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
	static int piper_stop(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] stopping device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
			plugin->timer->stop();
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
	 * Update the hardware pointer of the playback plugin. It checks the timer
	 * for overdue periods in the buffer and flushes them into the pipe.
	 */
	static snd_pcm_sframes_t piper_pointer(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] flusing device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
			Piper::Inlet* inlet = plugin->inlet.get();
			Piper::Timer* timer = plugin->timer.get();

			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t period = ioplug->period_size;
			const snd_pcm_uframes_t occupied = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);

			timer->try_accumulate(0);

			Piper::Inlet::Position position = inlet->start();
			unsigned int available = occupied / period;
			unsigned int outstanding = timer->consume();

			if (outstanding > available) {
				SNDERR("device %s cannot be flushed: insufficient data to flush\n", ioplug->name);
				return -EPIPE;
			}

			for (unsigned int i = 0; i < outstanding; i++) {
				inlet->preamble(position++).timestamp = Piper::now();
				inlet->flush();
			}

			snd_pcm_uframes_t start = ioplug->hw_ptr % capacity;
			snd_pcm_uframes_t copied = outstanding * period;
			return (start + copied) % capacity;

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be flushed: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be flushed: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Transfer data into the buffer of the playback plugin. It will copy data
	 * from the input into the appropriate writable blocks in the pipe.
	 */
	static snd_pcm_sframes_t piper_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *input_areas, snd_pcm_uframes_t input_start, snd_pcm_uframes_t input_size)
	{
		DPRINTF("[DEBUG] writing device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
			Piper::Inlet* inlet = plugin->inlet.get();

			const snd_pcm_uframes_t buffer_capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t buffer_occupied = difference(ioplug->hw_ptr, ioplug->appl_ptr, plugin->boundary);
			const snd_pcm_uframes_t buffer_unoccupied = buffer_capacity - buffer_occupied;

			if (input_size > buffer_unoccupied) {
				SNDERR("device %s cannot be written: insufficient space (%lu) for incoming data (%lu)", buffer_occupied, input_size);
				input_size = buffer_unoccupied;
			}

			const unsigned int channels = ioplug->channels;
			const snd_pcm_format_t format = ioplug->format;
			const snd_pcm_uframes_t target_capacity = ioplug->period_size;

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

			return static_cast<snd_pcm_sframes_t>(copied);

		} catch (std::exception& ex) {
			SNDERR("device %s cannot be flushed: %s\n", ioplug->name, ex.what());
			return -EBADFD;
		} catch (...) {
			SNDERR("device %s cannot be flushed: unknown exception\n", ioplug->name);
			return -EBADFD;
		}
	}

	/**
	 * Close the playback plugin. It will release any resource associated with
	 * the plugin.
	 */
	static int piper_close(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] closing device %s...\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
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
	SND_PCM_PLUGIN_DEFINE_FUNC(piper)
	{
		if (stream != SND_PCM_STREAM_PLAYBACK) {
			SNDERR("device %s cannot be initialized: device supports playback only", name);
			return -EINVAL;
		}

		snd_config_iterator_t i, next;
		const char* path;

		snd_config_for_each(i, next, conf) {
			snd_config_t* n = snd_config_iterator_entry(i);
			const char* id;

			if (snd_config_get_id(n, &id) < 0) {
				continue;
			} else if (std::strcmp(id, "comment") == 0 || std::strcmp(id, "type") == 0 || std::strcmp(id, "hint") == 0) {
				continue;
			} else if (std::strcmp(id, "path") != 0) {
				SNDERR("device %s cannot be initialized: unknown field %s in config", name, id);
				return -EINVAL;
			} else if (snd_config_get_string(n, &path) < 0) {
				SNDERR("device %s cannot be initialized: invalid path %s in config", name, path);
				return -EINVAL;
			}
		}

		try {
			std::unique_ptr<PiperPlugin> plugin(new PiperPlugin);

			plugin->pipe.reset(new Piper::Pipe(path));
			plugin->inlet.reset(new Piper::Inlet(plugin->pipe.get()));
			plugin->timer.reset(new Piper::Timer(plugin->pipe->period_time()));
			plugin->areas.resize(plugin->pipe->channels());

			plugin->io.version = SND_PCM_IOPLUG_VERSION;
			plugin->io.name = "ALSA <-> Piper PCM I/O Plugin";
			plugin->io.poll_fd = plugin->timer->descriptor();
			plugin->io.poll_events = POLLIN;
			plugin->io.mmap_rw = 0;
			plugin->io.callback = &plugin->callback;
			plugin->io.private_data = plugin.get();

			memset(&plugin->callback, 0, sizeof(plugin->callback));
			plugin->callback.sw_params = piper_sw_params;
			plugin->callback.start = piper_start;
			plugin->callback.stop = piper_stop;
			plugin->callback.pointer = piper_pointer;
			plugin->callback.transfer = piper_transfer;
			plugin->callback.close = piper_close;

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
			unsigned int buffers = static_cast<unsigned int>(plugin->pipe->buffer());
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
			} else if ((err = snd_pcm_ioplug_set_param_minmax(&plugin->io, SND_PCM_IOPLUG_HW_PERIODS, 2, buffers)) < 0) {
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

	SND_PCM_PLUGIN_SYMBOL(piper);

}


