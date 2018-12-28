

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

};


