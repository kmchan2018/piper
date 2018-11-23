

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
#include "buffer.hpp"
#include "pipe.hpp"
#include "timer.hpp"
#include "preamble.hpp"


//#define DPRINTF(s, ...) fprintf(stderr, (s), __VA_ARGS__)
#define DPRINTF(s, ...)


/**
 * Plugin parameters.
 */
struct PiperParams
{
	const char* path;
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
	unsigned int period;
	std::size_t frame_size;
	std::size_t period_size;

	explicit PiperParams(snd_config_t* conf)
	{
		snd_config_iterator_t i, next;
		const char* tempstr;
		long templong;
		ssize_t tempsize;
		unsigned int tempuint;
		
		snd_config_for_each(i, next, conf) {
			snd_config_t *n = snd_config_iterator_entry(i);
			const char *id;

			if (snd_config_get_id(n, &id) < 0) {
				continue;
			} else if (std::strcmp(id, "comment") == 0 || std::strcmp(id, "type") == 0 || std::strcmp(id, "hint") == 0) {
				continue;
			} else if (std::strcmp(id, "path") == 0) {
				if (snd_config_get_string(n, &path) < 0) {
					throw Piper::InvalidArgumentException("invalid path", "plugin.cpp", __LINE__);
				}
			} else if (std::strcmp(id, "format") == 0) {
				if (snd_config_get_string(n, &tempstr) < 0) {
					throw Piper::InvalidArgumentException("invalid format", "plugin.cpp", __LINE__);
				} else if ((format = snd_pcm_format_value(tempstr)) == SND_PCM_FORMAT_UNKNOWN) {
					throw Piper::InvalidArgumentException("invalid format", "plugin.cpp", __LINE__);
				}
			} else if (std::strcmp(id, "channels") == 0) {
				if (snd_config_get_integer(n, &templong) < 0) {
					throw Piper::InvalidArgumentException("invalid channels", "plugin.cpp", __LINE__);
				} else {
					channels = static_cast<unsigned int>(templong);
				}
			} else if (std::strcmp(id, "rate") == 0) {
				if (snd_config_get_integer(n, &templong) < 0) {
					throw Piper::InvalidArgumentException("invalid rate", "plugin.cpp", __LINE__);
				} else {
					rate = static_cast<unsigned int>(templong);
				}
			} else if (std::strcmp(id, "period_time") == 0) {
				if (snd_config_get_integer(n, &templong) < 0) {
					throw Piper::InvalidArgumentException("invalid period_time", "plugin.cpp", __LINE__);
				} else if (templong % 1000 != 0) {
					throw Piper::InvalidArgumentException("invalid period_time", "plugin.cpp", __LINE__);
				} else {
					period = static_cast<unsigned int>(templong / 1000);
				}
			} else {
				throw Piper::InvalidArgumentException("invalid field", "plugin.cpp", __LINE__);
			}
		}

		if ((tempsize = snd_pcm_format_size(format, channels)) < 0) {
			throw Piper::InvalidArgumentException("invalid format or channels", "plugins.cpp", __LINE__);
		} else if ((tempuint = tempsize * rate * period) % 1000 != 0) {
			throw Piper::InvalidArgumentException("invalid rate or period_time", "plugins.cpp", __LINE__);
		} else {
			frame_size = static_cast<std::size_t>(tempsize);
			period_size = static_cast<std::size_t>(tempuint / 1000);
		}
	}
};


/**
 * Plugin data.
 */
struct PiperPlugin
{
	snd_pcm_ioplug_t io;
	snd_pcm_ioplug_callback_t callback;
	snd_pcm_uframes_t boundary;
	std::unique_ptr<Piper::Timer> timer;
	std::unique_ptr<Piper::Backer> backer;
	std::unique_ptr<Piper::Pipe> pipe;
	std::unique_ptr<Piper::Inlet> inlet;
	std::unique_ptr<snd_pcm_channel_area_t[]> layout;
};


extern "C"
{

	static int piper_prepare(snd_pcm_ioplug_t* ioplug)
	{
		PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
		snd_pcm_sw_params_t* params;

		DPRINTF("[DEBUG] preparing device %s\n", ioplug->name);
		DPRINTF("[DEBUG] device %s has a boundary of %lu previously\n", ioplug->name, plugin->boundary);

		if (snd_pcm_sw_params_malloc(&params) < 0) {
			DPRINTF("[DEBUG] device %s cannot be prepared due to malloc error\n", ioplug->name);
			return -EIO;
		} else if (snd_pcm_sw_params_current(ioplug->pcm, params) < 0) {
			DPRINTF("[DEBUG] device %s cannot be prepared due to software parameters retrieval issue\n", ioplug->name);
			snd_pcm_sw_params_free(params);
			return -EIO;
		} else if (snd_pcm_sw_params_get_boundary(params, &plugin->boundary) < 0) {
			DPRINTF("[DEBUG] device %s cannot be prepared due to boundary retrieval issue\n", ioplug->name);
			snd_pcm_sw_params_free(params);
			return -EIO;
		} else {
			DPRINTF("[DEBUG] device %s has a boundary of %lu\n", ioplug->name, plugin->boundary);
			DPRINTF("[DEBUG] device %s is prepared\n", ioplug->name);
			snd_pcm_sw_params_free(params);
			return 0;
		}
	}

	static int piper_start(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] starting device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);

			if (plugin->inlet.get() != nullptr) {
				DPRINTF("[DEBUG] stopping active timer on device %s\n", ioplug->name);
				DPRINTF("[DEBUG] finishing active session %lu on device %s\n", plugin->inlet->session(), ioplug->name);
				plugin->timer->stop();
				plugin->inlet.reset(nullptr);
			}

			plugin->inlet.reset(new Piper::Inlet(*(plugin->pipe)));
			plugin->timer->start();

			DPRINTF("[DEBUG] starting active session %lu on device %s\n", plugin->inlet->session(), ioplug->name);
			DPRINTF("[DEBUG] device %s is started\n", ioplug->name);
			return 0;

		} catch (std::exception& ex) {
			DPRINTF("[DEBUG] device %s cannot be started due to %s\n", ioplug->name, ex.what());
			return -EIO;
		} catch (...) {
			DPRINTF("[DEBUG] device %s cannot be started due to unknown exception\n", ioplug->name);
			return -EIO;
		}
	}

	static int piper_stop(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] stopping device %s\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);

			if (plugin->inlet.get() != nullptr) {
				DPRINTF("[DEBUG] stopping active timer on device %s\n", ioplug->name);
				DPRINTF("[DEBUG] finishing active session %lu on device %s\n", plugin->inlet->session(), ioplug->name);
				plugin->timer->stop();
				plugin->inlet.reset(nullptr);
			}

			DPRINTF("[DEBUG] device %s is stopped\n", ioplug->name);
			return 0;

		} catch (std::exception& ex) {
			DPRINTF("[DEBUG] device %s cannot be stopped due to %s\n", ioplug->name, ex.what());
			return -EIO;
		} catch (...) {
			DPRINTF("[DEBUG] device %s cannot be stopped due to unknown exception\n", ioplug->name);
			return -EIO;
		}
	}

	static snd_pcm_sframes_t piper_pointer(snd_pcm_ioplug_t* ioplug)
	{
		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
			const snd_pcm_uframes_t boundary = plugin->boundary;
			const snd_pcm_uframes_t capacity = ioplug->buffer_size;
			const snd_pcm_uframes_t start = ioplug->hw_ptr;
			const snd_pcm_uframes_t until = ioplug->appl_ptr;
			snd_pcm_uframes_t available = (start <= until ? until - start : ((boundary - start) + until));
			snd_pcm_uframes_t pointer = start % capacity;

			plugin->timer->try_accumulate(0);

			if (plugin->timer->ticks() > 0) {
				const snd_pcm_uframes_t step = ioplug->period_size;
				snd_pcm_uframes_t outstanding = plugin->timer->ticks() * step;

				DPRINTF("[DEBUG] flusing device %s\n", ioplug->name);
				DPRINTF("[DEBUG] device %s needs to flush %lu frames and has %lu frames available from %lu to %lu in the buffer of %lu frames\n", ioplug->name, outstanding, available, start, until, capacity);
				DPRINTF("[DEBUG] device %s has hardware pointer at %lu before flushing\n", ioplug->name, pointer);

				plugin->timer->clear();

				if (available < outstanding) {
					DPRINTF("[DEBUG] device %s underruns and does not have sufficient data to flush\n", ioplug->name);
					return -EPIPE;
				}

				const size_t head_size = sizeof(Piper::Preamble);
				const size_t body_size = plugin->pipe->stride() - head_size;

				const unsigned int channels = ioplug->channels;
				const snd_pcm_format_t format = ioplug->format;
				const snd_pcm_channel_area_t* source = snd_pcm_ioplug_mmap_areas(ioplug);
				snd_pcm_channel_area_t* destination = plugin->layout.get();

				while (outstanding > 0) {
					Piper::Buffer staging(plugin->inlet->staging());
					Piper::Buffer head(staging.head(head_size));
					Piper::Buffer body(staging.tail(body_size));

					for (unsigned int i = 0; i < channels; i++) {
						destination->addr = body.start();
					}

					snd_pcm_uframes_t occupancy = 0;
					snd_pcm_uframes_t vacancy = step;

					while (vacancy > 0) {
						const snd_pcm_uframes_t occupant = std::min(capacity - pointer, step);
						snd_pcm_areas_copy(destination, occupancy, source, pointer, channels, occupant, format);
						pointer = (pointer + occupant) % capacity;
						outstanding -= occupant;
						available -= occupant;
						occupancy += occupant;
						vacancy -= occupant;
					}

					copy(head, Piper::Preamble());
					plugin->inlet->flush();
				}

				DPRINTF("[DEBUG] device %s has hardware pointer at %lu after flushing\n", ioplug->name, pointer);
				DPRINTF("[DEBUG] device %s is flushed\n", ioplug->name);
			}

			return pointer;

		} catch (std::exception& ex) {
			DPRINTF("[DEBUG] device %s cannot be flushed due to %s\n", ioplug->name, ex.what());
			return -EIO;
		} catch (...) {
			DPRINTF("[DEBUG] device %s cannot be flushed due to unknown exception\n", ioplug->name);
			return -EIO;
		}
	}

	static int piper_close(snd_pcm_ioplug_t* ioplug)
	{
		DPRINTF("[DEBUG] closing device %s...\n", ioplug->name);

		try {
			PiperPlugin* plugin = static_cast<PiperPlugin*>(ioplug->private_data);
			delete plugin;

			DPRINTF("[DEBUG] device %s is closed\n", ioplug->name);
			return 0;

		} catch (std::exception& ex) {
			DPRINTF("[DEBUG] device %s cannot be closed due to %s\n", ioplug->name, ex.what());
			return -EIO;
		} catch (...) {
			DPRINTF("[DEBUG] device %s cannot be closed due to unknown exception\n", ioplug->name);
			return -EIO;
		}
	}

	SND_PCM_PLUGIN_DEFINE_FUNC(piper)
	{
		static const snd_pcm_access_t access_list[] = {
			SND_PCM_ACCESS_RW_INTERLEAVED,
			SND_PCM_ACCESS_RW_NONINTERLEAVED,
			SND_PCM_ACCESS_MMAP_INTERLEAVED,
			SND_PCM_ACCESS_MMAP_NONINTERLEAVED
		};

		if (stream != SND_PCM_STREAM_PLAYBACK) {
			SNDERR("Cannot initialize device %s: device supports playback only", name);
			return -EINVAL;
		}

		try {
			int err;
			PiperParams params(conf);
			std::unique_ptr<PiperPlugin> plugin(new PiperPlugin);

			plugin->timer.reset(new Piper::Timer(params.period));
			plugin->backer.reset(new Piper::Backer(params.path));
			plugin->pipe.reset(new Piper::Pipe(*(plugin->backer)));
			plugin->layout.reset(new snd_pcm_channel_area_t[params.channels]);

			if (plugin->pipe->period() != static_cast<std::uint32_t>(params.period)) {
				SNDERR("Cannot initialize device %s: incompatible pipe", name);
				return -EINVAL;
			} else if (plugin->pipe->stride() != 	static_cast<std::uint32_t>(params.period_size + sizeof(Piper::Preamble))) {
				SNDERR("Cannot initialize device %s: incompatible pipe", name);
				return -EINVAL;
			}

			plugin->io.version = SND_PCM_IOPLUG_VERSION;
			plugin->io.name = "ALSA <-> Piper PCM I/O Plugin";
			plugin->io.poll_fd = plugin->timer->descriptor();
			plugin->io.poll_events = POLLIN;
			plugin->io.mmap_rw = 1;
			plugin->io.callback = &plugin->callback;
			plugin->io.private_data = plugin.get();

			memset(&plugin->callback, 0, sizeof(plugin->callback));
			plugin->callback.prepare = piper_prepare;
			plugin->callback.start = piper_start;
			plugin->callback.stop = piper_stop;
			plugin->callback.pointer = piper_pointer;
			plugin->callback.close = piper_close;

			for (unsigned int i = 0; i < params.channels; i++) {
				plugin->layout[i].addr = nullptr;
				plugin->layout[i].first = snd_pcm_format_physical_width(params.format) * i;
				plugin->layout[i].step = params.frame_size * 8;
			}

			if ((err = snd_pcm_ioplug_create(&plugin->io, name, stream, mode)) < 0) {
				SNDERR("Cannot initialize device %s: snd_pcm_ioplug_create returns error", name);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_ACCESS, 1, reinterpret_cast<const unsigned int*>(&access_list))) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_ACCESS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_FORMAT, 1, reinterpret_cast<const unsigned int*>(&params.format))) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_FORMAT", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, &params.channels)) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_CHANNELS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_RATE, 1, &params.rate)) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_RATE", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_list(&plugin->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 1, reinterpret_cast<const unsigned int*>(&params.period_size))) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIOD_BYTES", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			} else if ((err = snd_pcm_ioplug_set_param_minmax(&plugin->io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64)) < 0) {
				SNDERR("Cannot initialize device %s: cannot configure hardware parameter SND_PCM_IOPLUG_HW_PERIODS", name);
				snd_pcm_ioplug_delete(&plugin->io);
				return err;
			}

			*pcmp = plugin->io.pcm;
			plugin.release();
			return 0;

		} catch (std::exception& ex) {
			SNDERR("Cannot initialize device %s: %s", name, ex.what());
			return -EIO;
		} catch (...) {
			SNDERR("Cannot initialize device %s: unknown exception", name);
			return -EIO;
		}
	}

	SND_PCM_PLUGIN_SYMBOL(piper);

}


