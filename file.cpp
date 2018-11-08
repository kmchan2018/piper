

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"


namespace Piper {

	//////////////////////////////////////////////////////////////////////////
	//
	// Imports.
	//
	//////////////////////////////////////////////////////////////////////////

	using std::size_t;

	//////////////////////////////////////////////////////////////////////////
	//
	// Read all operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	File::ReadAll::ReadAll(const File& file, char* buffer, size_t remainder) :
		m_file(file),
		m_buffer(buffer),
		m_done(0),
		m_remainder(remainder)
	{
		// empty
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Write all operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	File::WriteAll::WriteAll(const File& file, const char* buffer, size_t remainder) :
		m_file(file),
		m_buffer(buffer),
		m_done(0),
		m_remainder(remainder)
	{
		// empty
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// File implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	File::File(int descriptor) :
		m_descriptor(descriptor)
	{
		if (descriptor < 0) {
			throw InvalidArgumentException("invalid descriptor", "file.cpp", __LINE__);
		}
	}

	File::File(const char* path, int flags) :
		m_descriptor(open(path, flags))
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
		m_descriptor(open(path, flags, mode))
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

	int File::read(Buffer destination)
	{
		ssize_t done = ::read(m_descriptor, destination.start(), destination.size());

		if (done >= 0) {
			return done;
		} else if (errno == EINTR) {
			return 0;
		} else if (errno == EAGAIN) {
			return 0;
		} else if (errno == EWOULDBLOCK) {
			return 0;
		} else if (errno == EPERM) {
			throw PermissionException("cannot read data from file due to permission", "file.cpp", __LINE__);
		} else {
			throw SystemException("cannot read data from file", "file.cpp", __LINE__);
		}
	}

	void File::read_all(Buffer destination)
	{
		File::ReadAll operation = read_all_async(destination);
		while (done(operation) == false) {
			execute(operation);
		}
	}

	File::ReadAll File::read_all_async(Buffer destination)
	{
		char* buffer = destination.start();
		size_t remainder = destination.size();
		return File::ReadAll(*this, buffer, remainder);
	}

	bool File::done(File::ReadAll& operation)
	{
		if (this == &operation.m_file) {
			return operation.m_remainder == 0;
		} else {
			throw InvalidArgumentException("invalid operation", "file.cpp", __LINE__);
		}
	}

	void File::execute(File::ReadAll& operation)
	{
		if (this == &operation.m_file) {
			if (operation.m_remainder > 0) {
				ssize_t done = ::read(m_descriptor, operation.m_buffer, operation.m_remainder);
				if (done > 0) {
					operation.m_buffer += done;
					operation.m_done += done;
					operation.m_remainder -= done;
				} else if (done == 0) {
					throw EOFException("end of file reached", "file.cpp", __LINE__);
				} else if (errno == EPERM) {
					throw PermissionException("cannot read data from file due to permission", "file.cpp", __LINE__);
				} else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
					throw SystemException("cannot read data from file", "file.cpp", __LINE__);
				}
			}
		} else {
			throw InvalidArgumentException("invalid operation", "file.cpp", __LINE__);
		}
	}

	int File::write(const Buffer& source)
	{
		ssize_t done = ::write(m_descriptor, source.start(), source.size());

		if (done >= 0) {
			return done;
		} else if (errno == EINTR) {
			return 0;
		} else if (errno == EAGAIN) {
			return 0;
		} else if (errno == EWOULDBLOCK) {
			return 0;
		} else if (errno == EPERM) {
			throw PermissionException("cannot write data to file due to permission", "file.cpp", __LINE__);
		} else {
			throw SystemException("cannot write data to file", "file.cpp", __LINE__);
		}
	}

	void File::write_all(Buffer source)
	{
		File::WriteAll operation = write_all_async(source);
		while (done(operation) == false) {
			execute(operation);
		}
	}

	File::WriteAll File::write_all_async(Buffer source)
	{
		const char* buffer = source.start();
		size_t remainder = source.size();
		return File::WriteAll(*this, buffer, remainder);
	}

	bool File::done(File::WriteAll& operation)
	{
		if (this == &operation.m_file) {
			return operation.m_remainder == 0;
		} else {
			throw InvalidArgumentException("invalid operation", "file.cpp", __LINE__);
		}
	}

	void File::execute(File::WriteAll& operation)
	{
		if (this == &operation.m_file) {
			if (operation.m_remainder > 0) {
				ssize_t done = ::write(m_descriptor, operation.m_buffer, operation.m_remainder);
				if (done > 0) {
					operation.m_buffer += done;
					operation.m_done += done;
					operation.m_remainder -= done;
				} else if (errno == EPERM) {
					throw PermissionException("cannot write data to file due to permission", "file.cpp", __LINE__);
				} else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
					throw SystemException("cannot write data to file", "file.cpp", __LINE__);
				}
			}
		} else {
			throw InvalidArgumentException("invalid operation", "file.cpp", __LINE__);
		}
	}

	void File::truncate(std::size_t length)
	{
		if (::ftruncate(m_descriptor, length) < 0) {
			switch (errno) {
				case EINVAL: throw InvalidArgumentException("invalid length", "file.cpp", __LINE__);
				case EPERM: throw PermissionException("cannot truncate the file due to permission", "file.cpp", __LINE__);
				default: throw SystemException("cannot truncate the file", "file.cpp", __LINE__);
			}
		}
	}

	int File::fcntl(int cmd)
	{
		return ::fcntl(m_descriptor, cmd);
	}

	int File::fcntl(int cmd, int arg)
	{
		return ::fcntl(m_descriptor, cmd, arg);
	}

	int File::fcntl(int cmd, void* arg)
	{
		return ::fcntl(m_descriptor, cmd, arg);
	}

};


