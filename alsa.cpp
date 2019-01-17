

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
			m_unit = static_cast<unsigned int>(frame_size);
			m_areas.resize(channels);
			m_offset = 0;
			m_length = 0;

			for (unsigned int i = 0; i < m_channels; i++) {
				m_areas[i].addr = nullptr;
				m_areas[i].first = static_cast<unsigned int>(sample_size) * i;
				m_areas[i].step = static_cast<unsigned int>(frame_size) * CHAR_BIT;
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

}


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

	static void alsa_ioplug_cb_trace(ALSA::IOPlug::Handle* handle, const char* event)
	{
		if (handle->trace) {
			std::fprintf(stderr, "%s\n", event);
		}
	}

	static int alsa_ioplug_cb_hw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_hw_params_t *params)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "hw_params callback called");
			implementation.hw_params(control, params);
			alsa_ioplug_cb_trace(handle, "hw_params callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "hw_params callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_hw_free(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "hw_free callback called");
			implementation.hw_free(control);
			alsa_ioplug_cb_trace(handle, "hw_free callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "hw_free callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_sw_params(snd_pcm_ioplug_t* ioplug, snd_pcm_sw_params_t *params)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);
		int err;
	
		if ((err = snd_pcm_sw_params_get_boundary(params, &handle->boundary)) < 0) {
			return err;
		}

		try {
			alsa_ioplug_cb_trace(handle, "sw_params callback called");
			implementation.sw_params(control, params);
			alsa_ioplug_cb_trace(handle, "sw_params callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "sw_params callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_prepare(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "prepare callback called");
			implementation.prepare(control);
			alsa_ioplug_cb_trace(handle, "prepare callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "prepare callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_start(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "start callback called");
			implementation.start(control);
			alsa_ioplug_cb_trace(handle, "start callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "start callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "start callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "start callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "start callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "start callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "start callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_stop(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "stop callback called");
			implementation.stop(control);
			alsa_ioplug_cb_trace(handle, "stop callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "stop callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_drain(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "drain callback called");
			implementation.drain(control);
			alsa_ioplug_cb_trace(handle, "drain callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "drain callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_pause(snd_pcm_ioplug_t* ioplug, int enable)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "pause callback called");
			implementation.pause(control, enable);
			alsa_ioplug_cb_trace(handle, "pause callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "pause callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_resume(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "resume callback called");
			implementation.resume(control);
			alsa_ioplug_cb_trace(handle, "resume callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "resume callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_descriptors_count(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback called");
			int result = implementation.poll_descriptors_count(control);
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors_count callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_descriptors(snd_pcm_ioplug_t* ioplug, struct pollfd *pfd, unsigned int space)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback called");
			int result = implementation.poll_descriptors(control, pfd, space);
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "poll_descriptors callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_poll_revents(snd_pcm_ioplug_t* ioplug, struct pollfd *pfd, unsigned int nfds, unsigned short *revents)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "poll_revents callback called");
			implementation.poll_revents(control, pfd, nfds, revents);
			alsa_ioplug_cb_trace(handle, "poll_revents callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "poll_revents callback failed with unknown error");
			return -EBADF;
		}
	}

	static snd_pcm_sframes_t alsa_ioplug_cb_pointer(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "pointer callback called");
			snd_pcm_sframes_t result = static_cast<snd_pcm_sframes_t>(implementation.pointer(control) % control.buffer_size());
			alsa_ioplug_cb_trace(handle, "pointer callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "pointer callback failed with unknown error");
			return -EBADF;
		}
	}

	static snd_pcm_sframes_t alsa_ioplug_cb_transfer(snd_pcm_ioplug_t* ioplug, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "transfer callback called");
			snd_pcm_sframes_t result = static_cast<snd_pcm_sframes_t>(implementation.transfer(control, areas, offset, size));
			alsa_ioplug_cb_trace(handle, "transfer callback completed");
			return result;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "transfer callback failed with unknown error");
			return -EBADF;
		}
	}

	static void alsa_ioplug_cb_dump(snd_pcm_ioplug_t* ioplug, snd_output_t* output)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "dump callback called");
			implementation.dump(control, output);
			alsa_ioplug_cb_trace(handle, "dump callback completed");
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "dump callback failed with unknown error");
		}
	}

	static int alsa_ioplug_cb_delay(snd_pcm_ioplug_t* ioplug, snd_pcm_sframes_t *delayp)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "delay callback called");
			implementation.delay(control, delayp);
			alsa_ioplug_cb_trace(handle, "delay callback completed");
			return 0;
		} catch (std::system_error& ex) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with system error");
			return negative(ex.code().value());
		} catch (std::bad_alloc& ex) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with memory error");
			return -ENOMEM;
		} catch (std::bad_cast& ex) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with cast error");
			return -EBADF;
		} catch (std::runtime_error& ex) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with logic error");
			return -EBADF;
		} catch (std::logic_error& ex) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with logic error");
			return -EBADF;
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "delay callback failed with unknown error");
			return -EBADF;
		}
	}

	static int alsa_ioplug_cb_close(snd_pcm_ioplug_t* ioplug)
	{
		ALSA::IOPlug::Handle* handle = static_cast<ALSA::IOPlug::Handle*>(ioplug->private_data);
		ALSA::IOPlug::Implementation& implementation = *(handle->implementation);
		ALSA::IOPlug::Control& control = *(handle->control);

		try {
			alsa_ioplug_cb_trace(handle, "close callback called");
			implementation.close(control);
		} catch (...) {
			alsa_ioplug_cb_trace(handle, "dump callback failed with unknown error");
		}

		delete handle;
		alsa_ioplug_cb_trace(handle, "close callback completed");
		return 0;
	}

}

namespace ALSA
{

	snd_pcm_uframes_t IOPlug::Control::buffer_used() noexcept
	{
		snd_pcm_stream_t stream = m_handle.ioplug.stream;
		snd_pcm_uframes_t boundary = m_handle.boundary;
		snd_pcm_uframes_t hw_ptr = m_handle.ioplug.hw_ptr;
		snd_pcm_uframes_t appl_ptr = m_handle.ioplug.appl_ptr;
		snd_pcm_uframes_t buffer = m_handle.ioplug.buffer_size;
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

	snd_pcm_uframes_t IOPlug::Control::buffer_free() noexcept
	{
		snd_pcm_uframes_t dev_buffer = m_handle.ioplug.buffer_size;
		snd_pcm_uframes_t dev_used = buffer_used();
		return dev_buffer - dev_used;
	}

	void IOPlug::Control::set_state(snd_pcm_state_t state)
	{
		int err = snd_pcm_ioplug_set_state(&m_handle.ioplug, state);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid state");
		} else if (err < 0) {
			throw error(err, snd_strerror(err));
		}
	}

	void IOPlug::Control::set_parameter_range(int type, unsigned int min, unsigned int max)
	{
		int err = snd_pcm_ioplug_set_param_minmax(&m_handle.ioplug, type, min, max);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid parameter type and/or range");
		} else if (err == -ENOMEM) {
			throw std::bad_alloc();
		} else if (err < 0) {
			throw error(err, snd_strerror(err));
		}
	}

	void IOPlug::Control::set_parameter_list(int type, unsigned int len, unsigned int* list)
	{
		int err = snd_pcm_ioplug_set_param_list(&m_handle.ioplug, type, len, list);

		if (err == -EINVAL) {
			throw std::invalid_argument("invalid parameter type and/or range");
		} else if (err == -ENOMEM) {
			throw std::bad_alloc();
		} else if (err < 0) {
			throw error(err, snd_strerror(err));
		}
	}

	snd_pcm_uframes_t IOPlug::Control::calculate_next_hardware_pointer(snd_pcm_uframes_t increment)
	{
		if (increment <= m_handle.ioplug.buffer_size) {
			return (m_handle.ioplug.hw_ptr + increment) % m_handle.boundary;
		} else {
			throw std::invalid_argument("hardware pointer increment larger than buffer size");
		}
	}

	IOPlug::IOPlug(const char* name, snd_pcm_stream_t stream, int mode, std::unique_ptr<ALSA::IOPlug::Implementation>&& implementation)
	{
		if (nullptr == name) {
			throw std::invalid_argument("invalid name");
		} else if (nullptr == implementation.get()) {
			throw std::invalid_argument("invalid implementation");
		}

		std::unique_ptr<Handle> data(new Handle);
		std::unique_ptr<Control> control(new Control(*data));
		Options options;
		int err;

		std::memset(&data->ioplug, 0, sizeof(snd_pcm_ioplug_t));
		std::memset(&data->callback, 0, sizeof(snd_pcm_ioplug_callback_t));

		implementation->configure(name, stream, mode, options);

		data->name = (options.name ? options.name : "Unknown Plugin");
		data->trace = false;
		data->control = std::move(control);
		data->implementation = std::move(implementation);
		data->ioplug.version = SND_PCM_IOPLUG_VERSION;
		data->ioplug.name = data->name.data();
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
				default: throw error(err, snd_strerror(err));
			}
		}

		m_handle = data.release();
		m_handle->implementation->create(*(m_handle->control));
	}

	IOPlug::~IOPlug()
	{
		if (m_handle != nullptr) {
			snd_pcm_ioplug_delete(&m_handle->ioplug);
		}
	}

	IOPlug::Implementation& IOPlug::implementation() const
	{
		if (m_handle != nullptr) {
			return *(m_handle->implementation);
		} else {
			throw std::runtime_error("ioplug device released");
		}
	}

	snd_pcm_t* IOPlug::release() noexcept
	{
		snd_pcm_t* result = m_handle->ioplug.pcm;
		m_handle = nullptr;
		return result;
	}

}


