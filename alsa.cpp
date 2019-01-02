

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <system_error>
#include <typeinfo>

#include <limits.h>
#include <poll.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/pcm_ioplug.h>

#include "exception.hpp"
#include "alsa.hpp"


//////////////////////////////////////////////////////////////////////////
//
// Range implementation.
//
//////////////////////////////////////////////////////////////////////////

namespace ALSA
{

	Range::Range(snd_pcm_format_t format, unsigned int channels)
	{
		ssize_t sample_size = snd_pcm_format_physical_width(format);
		ssize_t frame_size = snd_pcm_format_size(format, channels);

		if (channels <= 0) {
			throw std::invalid_argument("invalid channels");
		} else if (sample_size < 0) {
			throw std::invalid_argument("invalid format");
		} else if (frame_size < 0) {
			throw std::invalid_argument("invalid channels or format");
		} else {
			m_format = format;
			m_channels = channels;
			m_unit = frame_size;
			m_areas.resize(channels);
			m_offset = 0;
			m_length = 0;

			for (unsigned int i = 0; i < m_channels; i++) {
				m_areas[i].addr = nullptr;
				m_areas[i].first = sample_size * i;
				m_areas[i].step = frame_size * CHAR_BIT;
			}
		}
	}

	bool Range::valid() const noexcept
	{
		if (m_length == 0) {
			return false;
		} else if (m_offset == m_length) {
			return false;
		} else {
			return true;
		}
	}

	void Range::reset(char* buffer, std::size_t size)
	{
		if (buffer == nullptr) {
			throw std::invalid_argument("buffer cannot be nullptr");
		} else if (size % m_unit != 0) {
			throw std::invalid_argument("size cannot fall outside multiples of frame size");
		} else {
			m_offset = 0;
			m_length = size / m_unit;

			for (unsigned int i = 0; i < m_channels; i++) {
				m_areas[i].addr = buffer;
			}
		}
	}

	void Range::reset(const snd_pcm_channel_area_t* areas, snd_pcm_uframes_t length)
	{
		m_offset = 0;
		m_length = length;

		for (unsigned int i = 0; i < m_channels; i++) {
			m_areas[i].addr = areas[i].addr;
			m_areas[i].first = areas[i].first;
			m_areas[i].step = areas[i].step;
		}
	}

	void Range::behead(snd_pcm_uframes_t length)
	{
		if (length > m_length - m_offset) {
			throw std::invalid_argument("cannot behead more frames than the length of the range");
		} else {
			m_offset += length;
		}
	}

	snd_pcm_uframes_t Range::copy(Range& target, Range& source, snd_pcm_uframes_t maximum)
	{
		if (source.m_format != target.m_format) {
			throw std::invalid_argument("cannot copy data between incompatible source and target");
		} else if (source.m_channels != target.m_channels) {
			throw std::invalid_argument("cannot copy data between incompatible source and target");
		}

		if (source.m_length == 0) {
			return 0;
		} else if (target.m_length == 0) {
			return 0;
		} else {
			const unsigned int channels = source.m_channels;
			const snd_pcm_format_t format = source.m_format;
			const snd_pcm_channel_area_t* target_areas = target.m_areas.data();
			const snd_pcm_channel_area_t* source_areas = source.m_areas.data();
			const snd_pcm_uframes_t target_offset = target.m_offset;
			const snd_pcm_uframes_t source_offset = source.m_offset;
			const snd_pcm_uframes_t target_available = target.m_length - target.m_offset;
			const snd_pcm_uframes_t source_available = source.m_length - source.m_offset;
			const snd_pcm_uframes_t copied = std::min(maximum, std::min(target_available, source_available));

			if (snd_pcm_areas_copy(target_areas, target_offset, source_areas, source_offset, channels, copied, format) < 0) {
				throw std::logic_error("invalid data feed into snd_pcm_areas_copy");
			} else {
				return copied;
			}
		}
	}

	snd_pcm_uframes_t Range::copy_behead(Range& target, Range& source, snd_pcm_uframes_t maximum)
	{
		snd_pcm_uframes_t copied = copy(target, source, maximum);
		source.behead(copied);
		target.behead(copied);
		return copied;
	}

};


//////////////////////////////////////////////////////////////////////////
//
// ALSA IOPlug implementation.
//
//////////////////////////////////////////////////////////////////////////

static inline int negative(int input)
{
	return (input < 0 ? input : -input);
}

extern "C"
{

	static void alsa_ioplug_cb_trace(ALSA::IOPlug::Data* data, const char* event)
	{
		if (data->trace) {
			std::fprintf(stderr, "%s\n", event);
		}
	}

	static int alsa_ioplug_cb_hw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_hw_params_t *params)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "hw_params callback called");
			handler->hw_params(*handle, params);
			alsa_ioplug_cb_trace(data, "hw_params callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "hw_params callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_hw_free(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "hw_free callback called");
			handler->hw_free(*handle);
			alsa_ioplug_cb_trace(data, "hw_free callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "hw_free callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;
		int err;
	
		if ((err = snd_pcm_sw_params_get_boundary(params, &data->boundary)) < 0) {
			return err;
		}

		try {
			alsa_ioplug_cb_trace(data, "sw_params callback called");
			handler->sw_params(*handle, params);
			alsa_ioplug_cb_trace(data, "sw_params callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "sw_params callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_prepare(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "prepare callback called");
			handler->prepare(*handle);
			alsa_ioplug_cb_trace(data, "prepare callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "prepare callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_start(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "start callback called");
			handler->start(*handle);
			alsa_ioplug_cb_trace(data, "start callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "start callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "start callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "start callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "start callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "start callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "start callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_stop(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "stop callback called");
			handler->stop(*handle);
			alsa_ioplug_cb_trace(data, "stop callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "stop callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "stop callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "stop callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "stop callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "stop callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "stop callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_drain(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "drain callback called");
			handler->drain(*handle);
			alsa_ioplug_cb_trace(data, "drain callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "drain callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "drain callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "drain callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "drain callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "drain callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "drain callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_pause(snd_pcm_ioplug_t* ioplug, int enable)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "pause callback called");
			handler->pause(*handle, enable);
			alsa_ioplug_cb_trace(data, "pause callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "pause callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "pause callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "pause callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "pause callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "pause callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "pause callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_resume(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;
	
		try {
			alsa_ioplug_cb_trace(data, "resume callback called");
			handler->resume(*handle);
			alsa_ioplug_cb_trace(data, "resume callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "resume callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "resume callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "resume callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "resume callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "resume callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "resume callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback called");
			int result = handler->poll_descriptors_count(*handle);
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "poll_descriptors_count callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_descriptors(snd_pcm_ioplug_t* ioplug, struct pollfd *pfd, unsigned int space)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback called");
			int result = handler->poll_descriptors(*handle, pfd, space);
			alsa_ioplug_cb_trace(data, "poll_descriptors callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "poll_descriptors callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_revents(snd_pcm_ioplug_t* ioplug, struct pollfd *pfd, unsigned int nfds, unsigned short *revents)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "poll_revents callback called");
			handler->poll_revents(*handle, pfd, nfds, revents);
			alsa_ioplug_cb_trace(data, "poll_revents callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "poll_revents callback failed with unknown error");
			return -EBADF;
		}
	}

	static snd_pcm_sframes_t alsa_ioplug_cb_pointer(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "pointer callback called");
			snd_pcm_sframes_t result = handler->pointer(*handle) % handle->buffer_size();
			alsa_ioplug_cb_trace(data, "pointer callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "pointer callback failed with unknown error");
			return -EBADF;
		}
	}

	static snd_pcm_sframes_t alsa_ioplug_cb_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "transfer callback called");
			snd_pcm_sframes_t result = handler->transfer(*handle, areas, offset, size);
			alsa_ioplug_cb_trace(data, "transfer callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "transfer callback failed with unknown error");
			return -EBADF;
		}
	}

	static void alsa_ioplug_cb_dump(snd_pcm_ioplug_t* ioplug, snd_output_t* output)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;
	
		try {
			alsa_ioplug_cb_trace(data, "dump callback called");
			handler->dump(*handle, output);
			alsa_ioplug_cb_trace(data, "dump callback completed");
		} catch (...) {
			alsa_ioplug_cb_trace(data, "dump callback failed with unknown error");
		}
	}

	static int alsa_ioplug_cb_delay(snd_pcm_ioplug_t* ioplug, snd_pcm_sframes_t *delayp)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "delay callback called");
			handler->delay(*handle, delayp);
			alsa_ioplug_cb_trace(data, "delay callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(data, "delay callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(data, "delay callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(data, "delay callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(data, "delay callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(data, "delay callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(data, "delay callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_close(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Data* data = static_cast<ALSA::IOPlug::Data*>(ioplug->private_data);
		ALSA::IOPlug::Handler* handler = data->handle->handler();
		ALSA::IOPlug* handle = data->handle;

		try {
			alsa_ioplug_cb_trace(data, "close callback called");
			handler->close(*handle);
		} catch (...) {
			alsa_ioplug_cb_trace(data, "dump callback failed with unknown error");
		}

		delete handle;
		alsa_ioplug_cb_trace(data, "close callback completed");
		return 0;
	}

};

namespace ALSA
{

	snd_pcm_uframes_t IOPlug::buffer_used() noexcept
	{
		snd_pcm_stream_t stream = m_data->ioplug.stream;
		snd_pcm_uframes_t boundary = m_data->boundary;
		snd_pcm_uframes_t hw_ptr = m_data->ioplug.hw_ptr;
		snd_pcm_uframes_t appl_ptr = m_data->ioplug.appl_ptr;
		snd_pcm_uframes_t buffer = m_data->ioplug.buffer_size;
		snd_pcm_uframes_t used = 0;

		assert(stream == SND_PCM_STREAM_PLAYBACK || stream == SND_PCM_STREAM_CAPTURE);
		assert(boundary == 0 || hw_ptr < boundary);
		assert(boundary == 0 || appl_ptr < boundary);
		assert(buffer > 0);

		if (stream == SND_PCM_STREAM_PLAYBACK && appl_ptr >= hw_ptr) {
			used = appl_ptr - hw_ptr;
		} else if (stream == SND_PCM_STREAM_PLAYBACK && appl_ptr < hw_ptr) {
			used = (boundary - hw_ptr) + appl_ptr;
		} else if (stream == SND_PCM_STREAM_CAPTURE && hw_ptr >= appl_ptr) {
			used = hw_ptr - appl_ptr;
		} else if (stream == SND_PCM_STREAM_CAPTURE && hw_ptr < appl_ptr) {
			used = (boundary - appl_ptr) + hw_ptr;
		}

		assert(used <= buffer);
		return used;
	}

	snd_pcm_uframes_t IOPlug::buffer_free() noexcept
	{
		snd_pcm_uframes_t dev_buffer = m_data->ioplug.buffer_size;
		snd_pcm_uframes_t dev_used = buffer_used();
		return dev_buffer - dev_used;
	}

	void IOPlug::set_state(snd_pcm_state_t state)
	{
		int err = snd_pcm_ioplug_set_state(&m_data->ioplug, state);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid state");
		} else if (err < 0) {
			throw BaseException(snd_strerror(err), err);
		}
	}

	void IOPlug::set_parameter_range(int type, unsigned int min, unsigned int max)
	{
		int err = snd_pcm_ioplug_set_param_minmax(&m_data->ioplug, type, min, max);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid parameter type and/or range");
		} else if (err == -ENOMEM) {
			throw std::bad_alloc();
		} else if (err < 0) {
			throw BaseException(snd_strerror(err), err);
		}
	}

	void IOPlug::set_parameter_list(int type, unsigned int len, unsigned int* list)
	{
		int err = snd_pcm_ioplug_set_param_list(&m_data->ioplug, type, len, list);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid parameter type and/or range");
		} else if (err == -ENOMEM) {
			throw std::bad_alloc();
		} else if (err < 0) {
			throw BaseException(snd_strerror(err), err);
		}
	}

	snd_pcm_t* ALSA::IOPlug::open(const char* name, snd_pcm_stream_t stream, int mode, std::unique_ptr<ALSA::IOPlug::Handler>&& handler)
	{
		if (nullptr == name) {
			throw std::invalid_argument("invalid name");
		} else if (false == static_cast<bool>(handler)) {
			throw std::invalid_argument("invalid handler");
		}

		std::unique_ptr<Data> data(new Data);
		Options options;
		int err;

		std::memset(&data->ioplug, 0, sizeof(snd_pcm_ioplug_t));
		std::memset(&data->callback, 0, sizeof(snd_pcm_ioplug_callback_t));

		handler->configure(name, stream, mode, options);

		data->ioplug.version = SND_PCM_IOPLUG_VERSION;
		data->ioplug.name = (options.name ? options.name : "Unknown plugin");
		data->ioplug.flags = 0;
		data->ioplug.mmap_rw = 0;
		data->ioplug.poll_fd = options.poll_fd;
		data->ioplug.poll_events = options.poll_events;
		data->ioplug.callback = &data->callback;
		data->ioplug.private_data = data.get();
		data->callback.sw_params = alsa_ioplug_cb_sw_params;
		data->callback.start = alsa_ioplug_cb_start;
		data->callback.stop = alsa_ioplug_cb_stop;
		data->callback.pointer = alsa_ioplug_cb_pointer;
		data->callback.close = alsa_ioplug_cb_close;
		data->trace = false;

		if (options.mmap) data->ioplug.mmap_rw = 1;
		if (options.listed) data->ioplug.flags |= SND_PCM_IOPLUG_FLAG_LISTED;
		if (options.monotonic) data->ioplug.flags |= SND_PCM_IOPLUG_FLAG_MONOTONIC;
		if (options.enable_hw_params_callback) data->callback.hw_params = alsa_ioplug_cb_hw_params;
		if (options.enable_hw_free_callback) data->callback.hw_free = alsa_ioplug_cb_hw_free;
		if (options.enable_prepare_callback) data->callback.prepare = alsa_ioplug_cb_prepare;
		if (options.enable_drain_callback) data->callback.drain = alsa_ioplug_cb_drain;
		if (options.enable_pause_callback) data->callback.pause = alsa_ioplug_cb_pause;
		if (options.enable_resume_callback) data->callback.resume = alsa_ioplug_cb_resume;
		if (options.enable_poll_descriptors_count_callback) data->callback.poll_descriptors_count = alsa_ioplug_cb_poll_descriptors_count;
		if (options.enable_poll_descriptors_callback) data->callback.poll_descriptors = alsa_ioplug_cb_poll_descriptors;
		if (options.enable_poll_revents_callback) data->callback.poll_revents = alsa_ioplug_cb_poll_revents;
		if (options.enable_transfer_callback) data->callback.transfer = alsa_ioplug_cb_transfer;
		if (options.enable_dump_callback) data->callback.dump = alsa_ioplug_cb_dump;
		if (options.enable_delay_callback) data->callback.delay = alsa_ioplug_cb_delay;

		if (getenv("ALSA_IOPLUG_TRACE") != nullptr) {
			data->trace = true;
		}

		if ((err = snd_pcm_ioplug_create(&data->ioplug, name, stream, mode)) < 0) {
			switch (err) {
				case -EINVAL: throw std::invalid_argument("invalid name, stream or mode");
				case -ENOMEM: throw std::bad_alloc();
				default: throw BaseException(snd_strerror(err), err);
			}
		}

		snd_pcm_t* pcmp = data->ioplug.pcm;
		IOPlug::Data* datap = data.get();
		IOPlug::Handler* handlerp = handler.get();
		IOPlug* handlep = new IOPlug(std::move(data), std::move(handler));
		datap->handle = handlep;

		try {
			handlerp->create(*handlep);
		} catch (...) {
			snd_pcm_ioplug_delete(&datap->ioplug);
			throw;
		}

		return pcmp;
	}

};


