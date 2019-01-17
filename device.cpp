

#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <system_error>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"
#include "device.hpp"


#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "device.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "device.cpp", __LINE__);
#define EXC_ALSA(err) std::system_error(err, std::system_category(), strerror(err))


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
		try {
			m_file.writeall(buffer);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdoutPlaybackDevice::write] Cannot write to device due to IO error"));
		} catch (EndOfFileException& ex) {
			EXC_CHAIN(DeviceUnusableException("[Piper::StdoutPlaybackDevice::write] Cannot write to device due to end of file"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::write] Cannot write to device due to unwritable stdout"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::write] Cannot write to device due to logic error in underlying component"));
		}
	}

	void StdoutPlaybackDevice::try_write(Source& source)
	{
		try {
			m_file.try_writeall(source, -1);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to IO error"));
		} catch (EndOfFileException& ex) {
			EXC_CHAIN(DeviceUnusableException("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to end of file"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to unwritable stdout"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to logic error in underlying component"));
		}
	}

	void StdoutPlaybackDevice::try_write(Source& source, int timeout)
	{
		try {
			m_file.try_writeall(source, timeout);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to IO error"));
		} catch (EndOfFileException& ex) {
			EXC_CHAIN(DeviceUnusableException("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to end of file"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to unwritable stdout"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdoutPlaybackDevice::try_write] Cannot write to device due to logic error in underlying component"));
		}
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
		try {
			m_file.readall(buffer);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdinCaptureDevice::read] Cannot read from device due to IO error"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to unreadable stdin"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to logic error in underlying component"));
		}
	}

	void StdinCaptureDevice::try_read(Destination& destination)
	{
		try {
			m_file.try_readall(destination, -1);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdinCaptureDevice::read] Cannot read from device due to IO error"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to unreadable stdin"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to logic error in underlying component"));
		}
	}

	void StdinCaptureDevice::try_read(Destination& destination, int timeout)
	{
		try {
			m_file.try_readall(destination, timeout);
		} catch (FileIOException& ex) {
			EXC_CHAIN(DevicePlaybackException("[Piper::StdinCaptureDevice::read] Cannot read from device due to IO error"));
		} catch (FileNotWritableException& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to unreadable stdin"));
		} catch (std::logic_error& ex) {
			EXC_CHAIN(std::logic_error("[Piper::StdinCaptureDevice::read] Cannot read from device due to logic error in underlying component"));
		}
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
					return static_cast<snd_pcm_uframes_t>(written);
				} else if (written == -EAGAIN) {
					return 0;
				} else if (written == -EWOULDBLOCK) {
					return 0;
				} else if (written == -EINTR) {
					return 0;
				} else if (written == -EPIPE) {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DevicePlaybackException("[Piper::do_write_alsa_pcm] Cannot write to device due to buffer underrun"));
				} else if (written == -ESTRPIPE) {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DevicePlaybackException("[Piper::do_write_alsa_pcm] Cannot write to device due to suspension"));
				} else if (written == -EBADFD) {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to corruption"));
				} else if (written == -ENOTTY) {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to disconnection"));
				} else if (written == -ENODEV) {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to disconnection"));
				} else {
					EXC_START(EXC_ALSA(static_cast<int>(-written)), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device"));
				}
			} else if (err == -EINTR) {
				return 0;
			} else if (err == -EPIPE) {
				EXC_START(EXC_ALSA(-err), DevicePlaybackException("[Piper::do_write_alsa_pcm] Cannot write to device due to buffer underrun"));
			} else if (err == -ESTRPIPE) {
				EXC_START(EXC_ALSA(-err), DevicePlaybackException("[Piper::do_write_alsa_pcm] Cannot write to device due to suspension"));
			} else if (err == -EBADFD) {
				EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to corruption"));
			} else if (err == -ENOTTY) {
				EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to disconnection"));
			} else if (err == -ENODEV) {
				EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device due to disconnection"));
			} else {
				EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::do_write_alsa_pcm] Cannot write to device"));
			}
		}
	}

	AlsaPlaybackDevice::AlsaPlaybackDevice(const char* name)
	{
		int err = 0;

		if ((err = snd_pcm_open(&m_handle, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::AlsaPlaybackDevice] Cannot open device due to incorrect name"));
		} else if ((err = snd_pcm_nonblock(m_handle, 2)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::AlsaPlaybackDevice] Cannot switch device to non-blocking mode"));
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
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot initialize hardware parameters"));
		} else if ((err = snd_pcm_hw_params_set_rate_resample(m_handle, hwparams, 0)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on resampling"));
		} else if ((err = snd_pcm_hw_params_set_access(m_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on access mode"));
		} else if ((err = snd_pcm_hw_params_set_format(m_handle, hwparams, pipe.format_code_alsa())) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on format"));
		} else if ((err = snd_pcm_hw_params_set_channels(m_handle, hwparams, pipe.channels())) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on channels"));
		} else if ((err = snd_pcm_hw_params_set_rate(m_handle, hwparams, pipe.rate(), 0)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on rate"));
		} else if ((err = snd_pcm_hw_params_set_period_size_max(m_handle, hwparams, &device_period_size, &dir)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on period size"));
		} else if ((err = snd_pcm_hw_params_set_buffer_size_min(m_handle, hwparams, &device_buffer_size)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure hardware parameters on buffer size"));
		} else if ((err = snd_pcm_hw_params(m_handle, hwparams)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot commit hardware parameters"));
		}

		if ((err = snd_pcm_sw_params_current(m_handle, swparams)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot initialize software parameters"));
		} else if ((err = snd_pcm_sw_params_set_start_threshold(m_handle, swparams, device_period_size * 4)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure software parameters on start threshold"));
		} else if ((err = snd_pcm_sw_params_set_avail_min(m_handle, swparams, 1)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot configure software parameters on minimum available space"));
		} else if ((err = snd_pcm_sw_params(m_handle, swparams)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::configure] Cannot commit software parameters"));
		}
	}

	void AlsaPlaybackDevice::start()
	{
		int err = snd_pcm_prepare(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::start] Cannot prepare device due to corruption"));
		} else if (err == -ENOTTY) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::start] Cannot prepare device due to disconnection"));
		} else if (err == -ENODEV) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::start] Cannot prepare device due to disconnection"));
		} else if (err < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::start] Cannot prepare device"));
		}
	}

	void AlsaPlaybackDevice::stop()
	{
		int err = snd_pcm_drop(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::stop] Cannot prepare device due to corruption"));
		} else if (err == -ENOTTY) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::stop] Cannot prepare device due to disconnection"));
		} else if (err == -ENODEV) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::stop] Cannot prepare device due to disconnection"));
		} else if (err < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaPlaybackDevice::stop] Cannot prepare device due to ALSA error"));
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
					return static_cast<snd_pcm_uframes_t>(read);
				} else if (read == -EAGAIN) {
					return 0;
				} else if (read == -EWOULDBLOCK) {
					return 0;
				} else if (read == -EINTR) {
					return 0;
				} else if (read == -EPIPE) {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to buffer underrun"));
				} else if (read == -ESTRPIPE) {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to suspension"));
				} else if (read == -EBADFD) {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to corruption"));
				} else if (read == -ENOTTY) {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to disconnection"));
				} else if (read == -ENODEV) {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to disconnection"));
				} else {
					EXC_START(EXC_ALSA(static_cast<int>(-read)), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device"));
				}
			} else if (err == -EINTR) {
				return 0;
			} else if (err == -EPIPE) {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to buffer underrun"));
			} else if (err == -ESTRPIPE) {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to suspension"));
			} else if (err == -EBADFD) {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to corruption"));
			} else if (err == -ENOTTY) {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to disconnection"));
			} else if (err == -ENODEV) {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device due to disconnection"));
			} else {
				EXC_START(EXC_ALSA(-err), DeviceCaptureException("[Piper::do_read_alsa_pcm] Cannot read from device"));
			}
		}
	}

	AlsaCaptureDevice::AlsaCaptureDevice(const char* name)
	{
		int err = 0;

		if ((err = snd_pcm_open(&m_handle, name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::AlsaCaptureDevice] Cannot open device"));
		} else if ((err = snd_pcm_nonblock(m_handle, 2)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::AlsaCaptureDevice] Cannot switch device to non-blocking mode"));
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
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot initialize hardware parameters"));
		} else if ((err = snd_pcm_hw_params_set_rate_resample(m_handle, hwparams, 0)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on resampling"));
		} else if ((err = snd_pcm_hw_params_set_access(m_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on access mode"));
		} else if ((err = snd_pcm_hw_params_set_format(m_handle, hwparams, pipe.format_code_alsa())) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on format"));
		} else if ((err = snd_pcm_hw_params_set_channels(m_handle, hwparams, pipe.channels())) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on channels"));
		} else if ((err = snd_pcm_hw_params_set_rate(m_handle, hwparams, pipe.rate(), 0)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on rate"));
		} else if ((err = snd_pcm_hw_params_set_period_size_max(m_handle, hwparams, &device_period_size, &dir)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on period size"));
		} else if ((err = snd_pcm_hw_params_set_buffer_size_min(m_handle, hwparams, &device_buffer_size)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot configure hardware parameters on buffer size"));
		} else if ((err = snd_pcm_hw_params(m_handle, hwparams)) < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::configure] Cannot commit hardware parameters"));
		}
	}

	void AlsaCaptureDevice::start()
	{
		int err1 = snd_pcm_prepare(m_handle);

		if (err1 == -EBADFD) {
			EXC_START(EXC_ALSA(-err1), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot prepare device due to corruption"));
		} else if (err1 == -ENOTTY) {
			EXC_START(EXC_ALSA(-err1), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot prepare device due to disconnection"));
		} else if (err1 == -ENODEV) {
			EXC_START(EXC_ALSA(-err1), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot prepare device due to disconnection"));
		} else if (err1 < 0) {
			EXC_START(EXC_ALSA(-err1), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot prepare device"));
		}

		int err2 = snd_pcm_start(m_handle);

		if (err2 == -EBADFD) {
			EXC_START(EXC_ALSA(-err2), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot start device due to corruption"));
		} else if (err2 == -ENOTTY) {
			EXC_START(EXC_ALSA(-err2), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot start device due to disconnection"));
		} else if (err2 == -ENODEV) {
			EXC_START(EXC_ALSA(-err2), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot start device due to disconnection"));
		} else if (err2 < 0) {
			EXC_START(EXC_ALSA(-err2), DeviceUnusableException("[Piper::AlsaCaptureDevice::start] Cannot start device"));
		}

		m_partial_size = 0;
	}

	void AlsaCaptureDevice::stop()
	{
		int err = snd_pcm_drop(m_handle);
		m_partial_size = 0;

		if (err == -EBADFD) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::stop] Cannot stop device due to corruption"));
		} else if (err == -ENOTTY) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::stop] Cannot stop device due to disconnection"));
		} else if (err == -ENODEV) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::stop] Cannot stop device due to disconnection"));
		} else if (err < 0) {
			EXC_START(EXC_ALSA(-err), DeviceUnusableException("[Piper::AlsaCaptureDevice::stop] Cannot stop device"));
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

}


