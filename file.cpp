

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE
#define _FILE_OFFSET_BITS 64


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"


#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "file.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "file.cpp", __LINE__);
#define EXC_SYSTEM(err) std::system_error(err, std::system_category(), strerror(err))


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
		m_readable(false),
		m_writable(false),
		m_blocking(true)
	{
		if (m_descriptor >= 0) {
			int result = ::fcntl(m_descriptor, F_GETFL);

			if (result >= 0) {
				m_readable = (0 != (result & (O_RDWR | O_RDONLY)));
				m_writable = (0 != (result & (O_RDWR | O_WRONLY)));
				m_blocking = (0 == (result & O_NONBLOCK));

				// For some reason, access mode detection can fail on stdin/stdout/stderr.
				// In this case, we assign reasonable defaults for well-known descriptor,
				// or fail for user defined ones.

				if (m_readable == false && m_writable == false) {
					if (m_descriptor == STDIN_FILENO) {
						m_readable = true;
					} else if (m_descriptor == STDOUT_FILENO) {
						m_writable = true;
					} else if (m_descriptor == STDERR_FILENO) {
						m_writable = true;
					} else {
						EXC_START(std::logic_error("[Piper::File::File] Cannot use descriptor due to unknown access mode"));
					}
				}
			} else if (errno == EBADF) {
				EXC_START(std::invalid_argument("[Piper::File::File] Cannot use descriptor due to invalid descriptor"));
			} else {
				EXC_START(EXC_SYSTEM(errno), std::invalid_argument("[Piper::File::File] Cannot use descriptor due to operating system error"));
			}
		} else {
			EXC_START(std::invalid_argument("[Piper::File::File] Cannot use descriptor due to invalid descriptor"));
		}
	}

	File::File(const char* path, int flags) :
		m_descriptor(::open(path, flags)),
		m_readable(0 != (flags & (O_RDWR | O_RDONLY))),
		m_writable(0 != (flags & (O_RDWR | O_WRONLY))),
		m_blocking(0 == (flags & O_NONBLOCK))
	{
		if (m_descriptor < 0) {
			switch (errno) {
				case ELOOP: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to invalid path"));
				case ENAMETOOLONG: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to oversize path"));
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to invalid flags"));
				case EEXIST: EXC_START(FileExistException("[Piper::File::File] Cannot create existing file"));
				case ENOENT: EXC_START(FileNotExistException("[Piper::File::File] cannot open non-existing file"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::File] Cannot open file due to operating system error"));
			}
		}
	}

	File::File(const char* path, int flags, mode_t mode) :
		m_descriptor(::open(path, flags, mode)),
		m_readable(0 != (flags & (O_RDWR | O_RDONLY))),
		m_writable(0 != (flags & (O_RDWR | O_WRONLY))),
		m_blocking(0 == (flags & O_NONBLOCK))
	{
		if (m_descriptor < 0) {
			switch (errno) {
				case ELOOP: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to invalid path"));
				case ENAMETOOLONG: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to oversize path"));
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::File] Cannot open file due to invalid flags"));
				case EEXIST: EXC_START(FileExistException("[Piper::File::File] Cannot create existing file"));
				case ENOENT: EXC_START(FileNotExistException("[Piper::File::File] cannot open non-existing file"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::File] Cannot open file due to operating system error"));
			}
		}
	}

	File::~File()
	{
		if (m_descriptor >= 3) {
			::close(m_descriptor);
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
				case EBADF: EXC_START(std::logic_error("[Piper::File::fcntl] Cannot fcntl file due to stale descriptor"));
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::fcntl] Cannot fcntl file due to invalid fcntl cmd"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::fcntl] Cannot fcntl file due to operating system error"));
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
				case EBADF: EXC_START(std::logic_error("[Piper::File::fcntl] Cannot fcntl file due to stale descriptor"));
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::fcntl] Cannot fcntl file due to invalid fcntl cmd"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::fcntl] Cannot fcntl file due to operating system error"));
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
				case EBADF: EXC_START(std::logic_error("[Piper::File::fcntl] Cannot fcntl file due to stale descriptor"));
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::fcntl] Cannot fcntl file due to invalid fcntl cmd"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::fcntl] Cannot fcntl file due to operating system error"));
			}
		}
	}

	std::uint64_t File::tell()
	{
		std::int64_t result = lseek(m_descriptor, 0, SEEK_CUR);

		if (result >= 0) {
			return std::uint64_t(result);
		} else {
			switch (errno) {
				case EBADF: EXC_START(std::logic_error("[Piper::File::tell] Cannot check current position due to stale descriptor"));
				case ESPIPE: EXC_START(FileNotSeekableException("[Piper::File::tell] Cannot check current position due to unseekable descriptor"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::tell] Cannot check current position due to operating system error"));
			}
		}
	}

	void File::seek(std::int64_t offset, int origin)
	{
		if (lseek(m_descriptor, offset, origin) == -1) {
			switch (errno) {
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::seek] Cannot seek file due to invalid offset or origin"));
				case ENXIO: EXC_START(std::invalid_argument("[Piper::File::seek] Cannot seek file due to invalid offset or origin"));
				case EBADF: EXC_START(std::logic_error("[Piper::File::seek] Cannot seek file due to stale descriptor"));
				case ESPIPE: EXC_START(FileNotSeekableException("[Piper::File::seek] Cannot seek file due to unseekable descriptor"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::seek] Cannot seek file due to operating system error"));
			}
		}
	}

	size_t File::read(Buffer& buffer)
	{
		return read(std::move(buffer));
	}

	size_t File::read(Buffer&& buffer)
	{
		if (m_readable == false) {
			EXC_START(FileNotReadableException("[Piper::File::read] Cannot read file due to open mode"));
		}

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
			EXC_START(EndOfFileException("[Piper::File::read] Cannot read past the end of file"));
		} else if (done < 0 && errno == EBADF) {
			EXC_START(std::logic_error("[Piper::File::read] Cannot read file due to stale descriptor"));
		} else {
			EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::read] Cannot read file due to operating system error"));
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
		if (m_readable == false) {
			EXC_START(FileNotReadableException("[Piper::File::readall] Cannot read file due to open mode"));
		}

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
					EXC_START(std::logic_error("[Piper::File::readall] Cannot read file due to stale descriptor"));
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::readall] Cannot read file due to operating system error"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::readall] Cannot read file due to operating system error"));
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
		if (m_readable == false) {
			EXC_START(FileNotReadableException("[Piper::File::try_readall] Cannot read file due to open mode"));
		} else if (m_blocking && timeout >= 0) {
			EXC_START(FileMayBlockException("[Piper::File::try_readall] Cannot read file due to possible blocking"));
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
					EXC_START(std::logic_error("[Piper::File::try_readall] Cannot read file due to stale descriptor"));
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::try_readall] Cannot read file due to operating system error"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::try_readall] Cannot read file due to operating system error"));
				}
			}
		}
	}

	size_t File::write(const Buffer& source)
	{
		if (m_writable == false) {
			EXC_START(FileNotWritableException("[Piper::File::write] Cannot write file due to open mode"));
		}

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
			EXC_START(EndOfFileException("[Piper::File::write] Cannot write file due to closed receiver side"));
		} else if (errno == EBADF) {
			EXC_START(std::logic_error("[Piper::File::write] Cannot write file due to stale descriptor"));
		} else {
			EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::write] Cannot write file due to operating system error"));
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
		if (m_writable == false) {
			EXC_START(FileNotWritableException("[Piper::File::writeall] Cannot write file due to open mode"));
		}

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
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					EXC_START(EndOfFileException("[Piper::File::writeall] Cannot write file due to closed receiver side"));
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					EXC_START(std::logic_error("[Piper::File::writeall] Cannot write file due to stale descriptor"));
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::writeall] Cannot write file due to operating system error"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::writeall] Cannot write file due to operating system error"));
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
		if (m_writable == false) {
			EXC_START(FileNotWritableException("[Piper::File::try_writeall] Cannot write file due to open mode"));
		} else if (m_blocking && timeout >= 0) {
			EXC_START(FileMayBlockException("[Piper::File::try_writeall] Cannot write file due to possible blocking"));
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
				} else if (result > 0 && (pollfd.revents & POLLHUP) > 0) {
					EXC_START(EndOfFileException("[Piper::File::try_writeall] Cannot write file due to closed receiver side"));
				} else if (result > 0 && (pollfd.revents & POLLNVAL) > 0) {
					EXC_START(std::logic_error("[Piper::File::try_writeall] Cannot write file due to stale descriptor"));
				} else if (result > 0 && (pollfd.revents & POLLERR) > 0) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::try_writeall] Cannot write file due to operating system error"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::try_writeall] Cannot write file due to operating system error"));
				}
			}
		}
	}

	void File::truncate(std::size_t length)
	{
		if (m_writable == false) {
			EXC_START(FileNotWritableException("[Piper::File::truncate] Cannot truncate file due to open mode"));
		}

		if (::ftruncate(m_descriptor, length) < 0) {
			switch (errno) {
				case EINVAL: EXC_START(std::invalid_argument("[Piper::File::truncate] Cannot truncate file due to invalid length"));
				case EFBIG: EXC_START(std::invalid_argument("[Piper::File::truncate] Cannot truncate file due to invalid length"));
				case EBADF: EXC_START(std::logic_error("[Piper::File::truncate] Cannot truncate file due to stale descriptor"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::truncate] Cannot truncate file due to operating system error"));
			}
		}
	}

	void File::flush()
	{
		if (::fsync(m_descriptor) < 0) {
			switch (errno) {
				case EBADF: EXC_START(std::logic_error("[Piper::File::flush] Cannot flush file due to stale descriptor"));
				default: EXC_START(EXC_SYSTEM(errno), FileIOException("[Piper::File::flush] Cannot flush file due to operating system error"));
			}
		}
	}

}


