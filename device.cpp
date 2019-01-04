

#include <cassert>
#include <cstdio>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"
#include "device.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Standard output playback device implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	StdoutPlaybackDevice::StdoutPlaybackDevice() :
		m_file(STDOUT_FILENO)
	{
	}

	void StdoutPlaybackDevice::write(const Buffer buffer)
	{
		m_file.writeall(buffer);
	}

	void StdoutPlaybackDevice::try_write(Source& source)
	{
		m_file.try_writeall(source, -1);
	}

	void StdoutPlaybackDevice::try_write(Source& source, int timeout)
	{
		m_file.try_writeall(source, timeout);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Standard input capture device implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	StdinCaptureDevice::StdinCaptureDevice() :
		m_file(STDIN_FILENO)
	{
	}

	void StdinCaptureDevice::read(Buffer buffer)
	{
		m_file.readall(buffer);
	}

	void StdinCaptureDevice::try_read(Destination& destination)
	{
		m_file.try_readall(destination, -1);
	}

	void StdinCaptureDevice::try_read(Destination& destination, int timeout)
	{
		m_file.try_readall(destination, timeout);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// ALSA playback device implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	static snd_pcm_uframes_t do_write_alsa_pcm(snd_pcm_t* pcm, const char* buffer, snd_pcm_uframes_t count, int timeout)
	{
		while (true) {
			int err = snd_pcm_wait(pcm, timeout);

			if (err > 0) {
				snd_pcm_sframes_t written = snd_pcm_writei(pcm, buffer, count);

				if (written >= 0) {
					return written;
				} else if (written == -EAGAIN) {
					return 0;
				} else if (written == -EWOULDBLOCK) {
					return 0;
				} else if (written == -EINTR) {
					return 0;
				} else if (written == -EPIPE) {
					throw PlaybackException("cannot write to device due to buffer underrun", "device.cpp", __LINE__);
				} else if (written == -EBADFD) {
					throw PlaybackException("cannot write to device due to incorrect state", "device.cpp", __LINE__);
				} else if (written == -ESTRPIPE) {
					throw PlaybackException("cannot write to device due to suspension", "device.cpp", __LINE__);
				} else if (written == -ENOTTY) {
					throw DeviceException("cannot write to device due to disconnection", "device.cpp", __LINE__);
				} else if (written == -ENODEV) {
					throw DeviceException("cannot write to device due to disconnection", "device.cpp", __LINE__);
				} else {
					throw DeviceException("cannot write to device due to unknown reason", "device.cpp", __LINE__);
				}
			} else if (err == -EINTR) {
				return 0;
			} else if (err == -EPIPE) {
				throw PlaybackException("cannot write to device due to buffer underrun", "device.cpp", __LINE__);
			} else if (err == -EBADFD) {
				throw PlaybackException("cannot write to device due to incorrect state", "device.cpp", __LINE__);
			} else if (err == -ESTRPIPE) {
				throw PlaybackException("cannot write to device due to suspension", "device.cpp", __LINE__);
			} else if (err == -ENOTTY) {
				throw DeviceException("cannot write to device due to disconnection", "device.cpp", __LINE__);
			} else if (err == -ENODEV) {
				throw DeviceException("cannot write to device due to disconnection", "device.cpp", __LINE__);
			} else {
				throw DeviceException("cannot write to device due to unknown reason", "device.cpp", __LINE__);
			}
		}
	}

	AlsaPlaybackDevice::AlsaPlaybackDevice(const char* name)
	{
		int err = 0;

		if ((err = snd_pcm_open(&m_handle, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
			throw InvalidArgumentException("cannot open device", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_nonblock(m_handle, 2)) < 0) {
			throw InvalidArgumentException("cannot update device to non-blocking", "device.cpp", __LINE__);
		}

		m_frame_size = 0;
		m_partial_size = 0;
		m_partial_data = nullptr;
	}

	AlsaPlaybackDevice::~AlsaPlaybackDevice()
	{
		snd_pcm_close(m_handle);

		if (m_partial_data != nullptr) {
			delete[] m_partial_data;
			m_partial_data = nullptr;
		}
	}

	void AlsaPlaybackDevice::configure(const Pipe& pipe, unsigned int prebuffer)
	{
		if (m_partial_data != nullptr) {
			delete[] m_partial_data;
			m_partial_data = nullptr;
		}

		m_frame_size = pipe.frame_size();
		m_partial_size = 0;
		m_partial_data = new char[m_frame_size];

		snd_pcm_hw_params_t* hwparams = nullptr;
		snd_pcm_sw_params_t* swparams = nullptr;
		snd_pcm_uframes_t pipe_period_size = pipe.period_size() / m_frame_size;
		snd_pcm_uframes_t device_period_size = pipe_period_size;
		snd_pcm_uframes_t device_buffer_size = std::min(pipe_period_size * prebuffer, 2 * pipe_period_size);
		int dir, err;

		snd_pcm_hw_params_alloca(&hwparams);
		snd_pcm_sw_params_alloca(&swparams);

		if ((err = snd_pcm_hw_params_any(m_handle, hwparams)) < 0) {
			throw DeviceException("cannot initialize device hardware parameters", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_rate_resample(m_handle, hwparams, 0)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on resampling", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_access(m_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on access mode", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_format(m_handle, hwparams, pipe.format_code_alsa())) < 0) {
			throw DeviceException("cannot configure device hardware parameters on format", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_channels(m_handle, hwparams, pipe.channels())) < 0) {
			throw DeviceException("cannot configure device hardware parameters on channels", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_rate(m_handle, hwparams, pipe.rate(), 0)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on rate", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_period_size_max(m_handle, hwparams, &device_period_size, &dir)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on period size", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_buffer_size_min(m_handle, hwparams, &device_buffer_size)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on buffer size", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params(m_handle, hwparams)) < 0) {
			throw DeviceException("cannot commit device hardware parameters", "device.cpp", __LINE__);
		}

		if ((err = snd_pcm_sw_params_current(m_handle, swparams)) < 0) {
			throw DeviceException("cannot initialize device software parameters", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_sw_params_set_start_threshold(m_handle, swparams, device_period_size * 4)) < 0) {
			throw DeviceException("cannot configure device software parameters on start threshold", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_sw_params_set_avail_min(m_handle, swparams, 1)) < 0) {
			throw DeviceException("cannot configure device software parameters on minimum available space", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_sw_params(m_handle, swparams)) < 0) {
			throw DeviceException("cannot commit device software parameters", "device.cpp", __LINE__);
		}
	}

	void AlsaPlaybackDevice::start()
	{
		int err = snd_pcm_prepare(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			throw DeviceException("cannot prepare device due to incorrect state", "device.cpp", __LINE__);
		} else if (err == -ENOTTY) {
			throw DeviceException("cannot prepare device due to disconnection", "device.cpp", __LINE__);
		} else if (err == -ENODEV) {
			throw DeviceException("cannot prepare device due to disconnection", "device.cpp", __LINE__);
		} else if (err < 0) {
			throw DeviceException("cannot prepare device due to unknown reason", "device.cpp", __LINE__);
		}
	}

	void AlsaPlaybackDevice::stop()
	{
		int err = snd_pcm_drop(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			throw DeviceException("cannot stop playback due to incorrect state", "device.cpp", __LINE__);
		} else if (err == -ENOTTY) {
			throw DeviceException("cannot stop playback due to disconnection", "device.cpp", __LINE__);
		} else if (err == -ENODEV) {
			throw DeviceException("cannot stop playback due to disconnection", "device.cpp", __LINE__);
		} else if (err < 0) {
			throw DeviceException("cannot stop playback due to unknown reason", "device.cpp", __LINE__);
		}
	}

	void AlsaPlaybackDevice::write(const Buffer buffer)
	{
		Source source(buffer);

		while (source.remainder() > 0) {
			try_write(source, -1);
		}
	}

	void AlsaPlaybackDevice::try_write(Source& source)
	{
		try_write(source, -1);
	}

	void AlsaPlaybackDevice::try_write(Source& source, int timeout)
	{
		if (source.remainder() > 0) {
			Buffer buffer = source.data();
			const char* start = buffer.start();
			std::size_t pending = buffer.size();

			if (m_partial_size == m_frame_size) {
				if (do_write_alsa_pcm(m_handle, m_partial_data, 1, timeout) == 1) {
					m_partial_size = 0;
				}
			} else if (pending > 0) {
				if (m_partial_size > 0) {
					std::size_t copied = std::min(pending, m_frame_size - m_partial_size);
					std::memcpy(m_partial_data + m_partial_size, start, copied);
					m_partial_size += copied;
					source.consume(copied);

					if (m_partial_size == m_frame_size) {
						if (do_write_alsa_pcm(m_handle, m_partial_data, 1, timeout) == 1) {
							m_partial_size = 0;
						}
					}
				} else if (pending < m_frame_size) {
					std::memcpy(m_partial_data, start, pending);
					m_partial_size = pending;
					source.consume(pending);
				} else {
					source.consume(do_write_alsa_pcm(m_handle, start, pending / m_frame_size, timeout) * m_frame_size);
				}
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// ALSA capture device implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	static snd_pcm_uframes_t do_read_alsa_pcm(snd_pcm_t* pcm, char* buffer, snd_pcm_uframes_t count, int timeout)
	{
		while (true) {
			int err = snd_pcm_wait(pcm, timeout);

			if (err > 0) {
				snd_pcm_sframes_t read = snd_pcm_readi(pcm, buffer, count);

				if (read >= 0) {
					return read;
				} else if (read == -EAGAIN) {
					return 0;
				} else if (read == -EWOULDBLOCK) {
					return 0;
				} else if (read == -EINTR) {
					return 0;
				} else if (read == -EPIPE) {
					throw CaptureException("cannot read from device due to buffer overrun", "device.cpp", __LINE__);
				} else if (read == -EBADFD) {
					throw CaptureException("cannot read from device due to incorrect state", "device.cpp", __LINE__);
				} else if (read == -ESTRPIPE) {
					throw CaptureException("cannot read from device due to suspension", "device.cpp", __LINE__);
				} else if (read == -ENOTTY) {
					throw DeviceException("cannot read from device due to disconnection", "device.cpp", __LINE__);
				} else if (read == -ENODEV) {
					throw DeviceException("cannot read from device due to disconnection", "device.cpp", __LINE__);
				} else {
					throw DeviceException("cannot read from device due to unknown reason", "device.cpp", __LINE__);
				}
			} else if (err == -EINTR) {
				return 0;
			} else if (err == -EPIPE) {
				throw CaptureException("cannot read from device due to buffer overrun", "device.cpp", __LINE__);
			} else if (err == -EBADFD) {
				throw CaptureException("cannot read from device due to incorrect state", "device.cpp", __LINE__);
			} else if (err == -ESTRPIPE) {
				throw CaptureException("cannot read from device due to suspension", "device.cpp", __LINE__);
			} else if (err == -ENOTTY) {
				throw DeviceException("cannot read from device due to disconnection", "device.cpp", __LINE__);
			} else if (err == -ENODEV) {
				throw DeviceException("cannot read from device due to disconnection", "device.cpp", __LINE__);
			} else {
				throw DeviceException("cannot read from device due to unknown reason", "device.cpp", __LINE__);
			}
		}
	}

	AlsaCaptureDevice::AlsaCaptureDevice(const char* name)
	{
		int err = 0;

		if ((err = snd_pcm_open(&m_handle, name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
			throw InvalidArgumentException("cannot open device", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_nonblock(m_handle, 2)) < 0) {
			throw InvalidArgumentException("cannot update device to non-blocking", "device.cpp", __LINE__);
		}

		m_frame_size = 0;
		m_partial_size = 0;
		m_partial_data = nullptr;
	}

	AlsaCaptureDevice::~AlsaCaptureDevice()
	{
		snd_pcm_close(m_handle);

		if (m_partial_data != nullptr) {
			delete[] m_partial_data;
			m_partial_data = nullptr;
		}
	}

	void AlsaCaptureDevice::configure(const Pipe& pipe)
	{
		if (m_partial_data != nullptr) {
			delete[] m_partial_data;
			m_partial_data = nullptr;
		}

		m_frame_size = pipe.frame_size();
		m_partial_size = 0;
		m_partial_data = new char[m_frame_size];

		snd_pcm_hw_params_t* hwparams = nullptr;
		snd_pcm_sw_params_t* swparams = nullptr;
		snd_pcm_uframes_t pipe_period_size = pipe.period_size() / m_frame_size;
		snd_pcm_uframes_t device_period_size = pipe_period_size;
		snd_pcm_uframes_t device_buffer_size = 2 * pipe_period_size;
		int dir, err;

		snd_pcm_hw_params_alloca(&hwparams);
		snd_pcm_sw_params_alloca(&swparams);

		if ((err = snd_pcm_hw_params_any(m_handle, hwparams)) < 0) {
			throw DeviceException("cannot initialize device hardware parameters", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_rate_resample(m_handle, hwparams, 0)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on resampling", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_access(m_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on access mode", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_format(m_handle, hwparams, pipe.format_code_alsa())) < 0) {
			throw DeviceException("cannot configure device hardware parameters on format", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_channels(m_handle, hwparams, pipe.channels())) < 0) {
			throw DeviceException("cannot configure device hardware parameters on channels", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_rate(m_handle, hwparams, pipe.rate(), 0)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on rate", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_period_size_max(m_handle, hwparams, &device_period_size, &dir)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on period size", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params_set_buffer_size_min(m_handle, hwparams, &device_buffer_size)) < 0) {
			throw DeviceException("cannot configure device hardware parameters on buffer size", "device.cpp", __LINE__);
		} else if ((err = snd_pcm_hw_params(m_handle, hwparams)) < 0) {
			throw DeviceException("cannot commit device hardware parameters", "device.cpp", __LINE__);
		}
	}

	void AlsaCaptureDevice::start()
	{
		int err1 = snd_pcm_prepare(m_handle);

		if (err1 == -EBADFD) {
			throw DeviceException("cannot prepare device due to incorrect state", "device.cpp", __LINE__);
		} else if (err1 == -ENOTTY) {
			throw DeviceException("cannot prepare device due to disconnection", "device.cpp", __LINE__);
		} else if (err1 == -ENODEV) {
			throw DeviceException("cannot prepare device due to disconnection", "device.cpp", __LINE__);
		} else if (err1 < 0) {
			throw DeviceException("cannot prepare device due to unknown reason", "device.cpp", __LINE__);
		}

		int err2 = snd_pcm_start(m_handle);

		if (err2 == -EBADFD) {
			throw DeviceException("cannot start capture due to incorrect state", "device.cpp", __LINE__);
		} else if (err2 == -ENOTTY) {
			throw DeviceException("cannot start capture due to disconnection", "device.cpp", __LINE__);
		} else if (err2 == -ENODEV) {
			throw DeviceException("cannot start capture due to disconnection", "device.cpp", __LINE__);
		} else if (err2 < 0) {
			throw DeviceException("cannot start capture due to unknown reason", "device.cpp", __LINE__);
		}

		m_partial_size = 0;
	}

	void AlsaCaptureDevice::stop()
	{
		int err = snd_pcm_drop(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			throw DeviceException("cannot stop capture due to incorrect state", "device.cpp", __LINE__);
		} else if (err == -ENOTTY) {
			throw DeviceException("cannot stop capture due to disconnection", "device.cpp", __LINE__);
		} else if (err == -ENODEV) {
			throw DeviceException("cannot stop capture due to disconnection", "device.cpp", __LINE__);
		} else if (err < 0) {
			throw DeviceException("cannot stop capture due to unknown reason", "device.cpp", __LINE__);
		}
	}

	void AlsaCaptureDevice::read(Buffer buffer)
	{
		Destination destination(buffer);

		while (destination.remainder() > 0) {
			try_read(destination, -1);
		}
	}

	void AlsaCaptureDevice::try_read(Destination& destination)
	{
		try_read(destination, -1);
	}

	void AlsaCaptureDevice::try_read(Destination& destination, int timeout)
	{
		if (destination.remainder() > 0) {
			Buffer buffer = destination.data();
			char* start = buffer.start();
			std::size_t pending = buffer.size();

			assert(start != nullptr);
			assert(pending > 0);

			if (m_partial_size > 0) {
				std::size_t copied = std::min(pending, m_partial_size);
				std::memcpy(start, m_partial_data + (m_frame_size - m_partial_size), copied);
				destination.consume(copied);
				m_partial_size -= copied;
			} else if (pending >= m_frame_size) {
				std::size_t copied = m_frame_size * do_read_alsa_pcm(m_handle, start, pending / m_frame_size, timeout);
				destination.consume(copied);
			} else if (do_read_alsa_pcm(m_handle, m_partial_data, 1, timeout) == 1) {
				std::memcpy(start, m_partial_data, pending);
				destination.consume(pending);
				m_partial_size = m_frame_size - pending;
			}
		}
	}

};


