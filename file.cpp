

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "transfer.hpp"
#include "file.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Imports.
	//
	//////////////////////////////////////////////////////////////////////////

	using std::size_t;

	//////////////////////////////////////////////////////////////////////////
	//
	// File implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	File::File(int descriptor) :
		m_descriptor(descriptor),
		m_blocking(true)
	{
		if (descriptor >= 0) {
			fcntl(F_GETFL);
		} else {
			throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
		}
	}

	File::File(const char* path, int flags) :
		m_descriptor(open(path, flags)),
		m_blocking(0 == (flags & O_NONBLOCK))
	{
		if (m_descriptor < 0) {
			switch (errno) {
				case ELOOP: throw InvalidArgumentException("invalid path", "file.cpp", __LINE__);
				case ENAMETOOLONG: throw InvalidArgumentException("invalid path", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid flags", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("permission error", "file.cpp", __LINE__);
				case EEXIST: throw SystemException("cannot create existing file", "file.cpp", __LINE__);
				case ENOENT: throw SystemException("cannot open non-existing file", "file.cpp", __LINE__);
				default: throw SystemException("cannot open file", "file.cpp", __LINE__);
			}
		}
	}

	File::File(const char* path, int flags, mode_t mode) :
		m_descriptor(open(path, flags, mode)),
		m_blocking(0 == (flags & O_NONBLOCK))
	{
		if (m_descriptor < 0) {
			switch (errno) {
				case ELOOP: throw InvalidArgumentException("invalid path", "file.cpp", __LINE__);
				case ENAMETOOLONG: throw InvalidArgumentException("invalid path", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid flags", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("permission error", "file.cpp", __LINE__);
				case EEXIST: throw SystemException("cannot create existing file", "file.cpp", __LINE__);
				case ENOENT: throw SystemException("cannot open non-existing file", "file.cpp", __LINE__);
				default: throw SystemException("cannot open file", "file.cpp", __LINE__);
			}
		}
	}

	File::~File()
	{
		if (m_descriptor >= 3) {
			close(m_descriptor);
		}
	}

	int File::fcntl(int cmd)
	{
		int result = ::fcntl(m_descriptor, cmd);

		if (cmd == F_GETFL && result >= 0) {
			m_blocking = (0 == (result & O_NONBLOCK));
			return result;
		} else if (result >= 0) {
			return result;
		} else {
			switch (errno) {
				case EBADF: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid cmd or arg", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot fcntl descriptor due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot fcntl descriptor", "file.cpp", __LINE__);
			}
		}
	}

	int File::fcntl(int cmd, int arg)
	{
		int result = ::fcntl(m_descriptor, cmd, arg);

		if (cmd == F_GETFL && result >= 0) {
			m_blocking = (0 == (result & O_NONBLOCK));
			return result;
		} else if (cmd == F_SETFL && result >= 0) {
			m_blocking = (0 == (arg & O_NONBLOCK));
			return result;
		} else if (result >= 0) {
			return result;
		} else {
			switch (errno) {
				case EBADF: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid cmd or arg", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot fcntl descriptor due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot fcntl descriptor", "file.cpp", __LINE__);
			}
		}
	}

	int File::fcntl(int cmd, void* arg)
	{
		int result = ::fcntl(m_descriptor, cmd, arg);

		if (cmd == F_GETFL && result >= 0) {
			m_blocking = (0 == (result & O_NONBLOCK));
			return result;
		} else if (result >= 0) {
			return result;
		} else {
			switch (errno) {
				case EBADF: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid cmd or arg", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot fcntl descriptor due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot fcntl descriptor", "file.cpp", __LINE__);
			}
		}
	}

	size_t File::read(Buffer& buffer)
	{
		return read(std::move(buffer));
	}

	size_t File::read(Buffer&& buffer)
	{
		void* start = buffer.start();
		size_t size = buffer.size();
		ssize_t done = ::read(m_descriptor, start, size);

		if (done > 0) {
			return done;
		} else if (done < 0 && errno == EINTR) {
			return 0;
		} else if (done < 0 && errno == EAGAIN) {
			return 0;
		} else if (done < 0 && errno == EWOULDBLOCK) {
			return 0;
		} else if (done == 0) {
			throw EOFException("cannot read past the end of file", "file.cpp", __LINE__);
		} else if (errno == EPERM) {
			throw PermissionException("cannot read data from file due to permission", "file.cpp", __LINE__);
		} else {
			throw SystemException("cannot read data from file", "file.cpp", __LINE__);
		}
	}

	void File::read(Destination& destination)
	{
		read(std::move(destination));
	}

	void File::read(Destination&& destination)
	{
		destination.consume(read(destination.data()));
	}

	void File::readall(Buffer& buffer)
	{
		readall(Destination(buffer));
	}

	void File::readall(Buffer&& buffer)
	{
		readall(Destination(buffer));
	}

	void File::readall(Destination& destination)
	{
		readall(std::move(destination));
	}

	void File::readall(Destination&& destination)
	{
		while (destination.remainder() > 0) {
			if (m_blocking) {
				read(destination);
			} else {
				// If the file is non-blocking, the read method will return immediately,
				// turning this loop into a busy loop. To avoid this situation, we need
				// to use poll(2) to yield to the operating system and wait until the
				// descriptor is readable.

				struct pollfd pollfd;
				pollfd.fd = m_descriptor;
				pollfd.events = POLLIN;

				int result = ::poll(&pollfd, 1, -1);

				if (result > 0 && (pollfd.revents & POLLIN) > 0) {
					read(destination);
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					read(destination);
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					throw SystemException("cannot read data from file", "file.cpp", __LINE__);
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot read data from file", "file.cpp", __LINE__);
				}
			}
		}
	}

	void File::try_readall(Destination& destination)
	{
		try_readall(std::move(destination), -1);
	}

	void File::try_readall(Destination&& destination)
	{
		try_readall(destination, -1);
	}

	void File::try_readall(Destination& destination, int timeout)
	{
		try_readall(std::move(destination), timeout);
	}

	void File::try_readall(Destination&& destination, int timeout)
	{
		if (m_blocking && timeout >= 0) {
			throw InvalidStateException("cannot read blocking descriptor with timeout", "file.cpp", __LINE__);
		}

		if (destination.remainder() > 0) {
			if (m_blocking) {
				read(destination);
			} else {
				// If the file is non-blocking, the read method will return immediately,
				// turning this loop into a busy loop. To avoid this situation, we need
				// to use poll(2) to yield to the operating system and wait until the
				// descriptor is readable.

				struct pollfd pollfd;
				pollfd.fd = m_descriptor;
				pollfd.events = POLLIN;

				int result = ::poll(&pollfd, 1, timeout);

				if (result > 0 && (pollfd.revents & POLLIN) > 0) {
					read(destination);
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					read(destination);
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					throw SystemException("cannot read data from file", "file.cpp", __LINE__);
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot read data from file", "file.cpp", __LINE__);
				}
			}
		}
	}

	size_t File::write(const Buffer& source)
	{
		const char* start = source.start();
		size_t size = source.size();
		ssize_t done = ::write(m_descriptor, start, size);

		if (done >= 0) {
			return done;
		} else if (errno == EINTR) {
			return 0;
		} else if (errno == EAGAIN) {
			return 0;
		} else if (errno == EWOULDBLOCK) {
			return 0;
		} else if (errno == EPIPE) {
			throw EOFException("cannot write data to closed pipe/socket", "file.cpp", __LINE__);
		} else if (errno == EPERM) {
			throw PermissionException("cannot write data to file due to permission", "file.cpp", __LINE__);
		} else {
			throw SystemException("cannot write data to file", "file.cpp", __LINE__);
		}
	}

	void File::write(Source& source)
	{
		write(std::move(source));
	}

	void File::write(Source&& source)
	{
		source.consume(write(source.data()));
	}

	void File::writeall(const Buffer& buffer)
	{
		writeall(Source(buffer));
	}

	void File::writeall(Source& source)
	{
		writeall(std::move(source));
	}

	void File::writeall(Source&& source)
	{
		while (source.remainder() > 0) {
			if (m_blocking) {
				write(source);
			} else {
				// If the file is non-blocking, the write method will return immediately,
				// turning this loop into a busy loop. To avoid this situation, we need
				// to use poll(2) to yield to the operating system and wait until the
				// descriptor is writable.

				struct pollfd pollfd;
				pollfd.fd = m_descriptor;
				pollfd.events = POLLOUT;

				int result = ::poll(&pollfd, 1, -1);

				if (result > 0 && (pollfd.revents & POLLOUT) > 0) {
					write(source);
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					throw EOFException("cannot write data to closed pipe/socket", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					throw SystemException("cannot write data to file", "file.cpp", __LINE__);
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot write data to file", "file.cpp", __LINE__);
				}
			}
		}
	}

	void File::try_writeall(Source& source)
	{
		try_writeall(std::move(source), -1);
	}

	void File::try_writeall(Source&& source)
	{
		try_writeall(source, -1);
	}

	void File::try_writeall(Source& source, int timeout)
	{
		try_writeall(std::move(source), timeout);
	}

	void File::try_writeall(Source&& source, int timeout)
	{
		if (m_blocking && timeout >= 0) {
			throw InvalidStateException("cannot write blocking descriptor with timeout", "file.cpp", __LINE__);
		}

		if (source.remainder() > 0) {
			if (m_blocking) {
				write(source);
			} else {
				// If the file is non-blocking, the write method will return immediately,
				// turning this loop into a busy loop. To avoid this situation, we need
				// to use poll(2) to yield to the operating system and wait until the
				// descriptor is writable.

				struct pollfd pollfd;
				pollfd.fd = m_descriptor;
				pollfd.events = POLLOUT;

				int result = ::poll(&pollfd, 1, -1);

				if (result > 0 && (pollfd.revents & POLLOUT) > 0) {
					write(source);
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					throw EOFException("cannot write data to closed pipe/socket", "file.cpp", __LINE__);
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					throw SystemException("cannot write data to file", "file.cpp", __LINE__);
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot write data to file", "file.cpp", __LINE__);
				}
			}
		}
	}

	void File::truncate(std::size_t length)
	{
		if (::ftruncate(m_descriptor, length) < 0) {
			switch (errno) {
				case EBADF: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EROFS: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case ETXTBSY: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid descriptor or length", "file.cpp", __LINE__);
				case EFBIG: throw InvalidArgumentException("invalid length", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot truncate the file due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot truncate the file", "file.cpp", __LINE__);
			}
		}
	}

	void File::flush()
	{
		if (::fsync(m_descriptor) < 0) {
			switch (errno) {
				case EBADF: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EROFS: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot flush the file due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot flush the file", "file.cpp", __LINE__);
			}
		}
	}

};


