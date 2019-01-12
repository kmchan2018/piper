

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "exception.hpp"
#include "buffer.hpp"


#ifndef FILE_HPP_
#define FILE_HPP_


namespace Piper
{

	/**
	 * File is a RAII wrapper class that manages a file descriptor.
	 */
	class File
	{
		public:

			/**
			 * Construct a file instance from the file descriptor. Throws exception when
			 * the given descriptor is invalid.
			 */
			explicit File(int descriptor);

			/**
			 * Construct a file instance by opening the given file path. Throws exception
			 * when the open syscall fails.
			 */
			explicit File(const char* path, int flags);

			/**
			 * Construct a file instance by opening the given file path. Throws exception
			 * when the open syscall fails.
			 */
			explicit File(const char* path, int flags, mode_t mode);

			/**
			 * Destruct the instance by closing the descriptor. Note that the class
			 * will not attempt to close the standard file descriptors including
			 * stdin, stdout and stderr.
			 */
			~File();

			/**
			 * Return the descriptor managed by this file instance.
			 */
			int descriptor() const noexcept
			{
				return m_descriptor;
			}

			/**
			 * Return whether the file is readable.
			 */
			bool readable() const noexcept
			{
				return m_readable;
			}

			/**
			 * Return whether the file is writable.
			 */
			bool writable() const noexcept
			{
				return m_writable;
			}

			/**
			 * Return the blocking mode of the descriptor.
			 */
			bool blocking() const noexcept
			{
				return m_blocking;
			}

			/**
			 * Configure the file descriptor.
			 */
			int fcntl(int cmd);

			/**
			 * Configure the file descriptor.
			 */
			int fcntl(int cmd, int arg);

			/**
			 * Configure the file descriptor.
			 */
			int fcntl(int cmd, void* arg);

			/**
			 * Return the current position of the file descriptor.
			 */
			std::uint64_t tell();

			/**
			 * Seek the file descriptor to the specified position.
			 */
			void seek(std::int64_t offset, int origin);

			/**
			 * Read data from the file into the buffer. The method will read data
			 * from the file ONCE, save the result into the beginning of buffer and
			 * return the number of bytes saved to the buffer. The method implements
			 * slightly different behavior when the descriptor is under different
			 * mode.
			 *
			 * When the descriptor is under blocking mode, the method will block
			 * until enough data is read or when the process receives a POSIX
			 * signal.
			 *
			 * On the other hand, when the descriptor is under non-blocking mode,
			 * the method will save any available data and return immediately. In
			 * this case the method may return without reading anything.
			 */
			std::size_t read(Buffer& buffer);

			/**
			 * Read data from the file into the buffer. The method will read data
			 * from the file ONCE, save the result into the beginning of buffer and
			 * return the number of bytes saved to the buffer. The method implements
			 * slightly different behavior when the descriptor is under different
			 * mode.
			 *
			 * When the descriptor is under blocking mode, the method will block
			 * until enough data is read or when the process receives a POSIX
			 * signal.
			 *
			 * On the other hand, when the descriptor is under non-blocking mode,
			 * the method will save any available data and return immediately. In
			 * this case the method may return without reading anything.
			 */
			std::size_t read(Buffer&& buffer);

			/**
			 * Read data from the file into the destination. The method will read
			 * data from the file ONCE, save the result into the destination and 
			 * update the destination counter accordingly. The method implements
			 * slightly different behavior when the descriptor is under different
			 * mode.
			 *
			 * When the descriptor is under blocking mode, the method will block
			 * until enough data is read or when the process receives a POSIX
			 * signal.
			 *
			 * On the other hand, when the descriptor is under non-blocking mode,
			 * the method will save any available data and return immediately. In
			 * this case the method may return without reading anything.
			 */
			void read(Destination& destination);

			/**
			 * Read data from the file into the destination. The method will read
			 * data from the file ONCE, save the result into the destination and 
			 * update the destination counter accordingly. The method implements
			 * slightly different behavior when the descriptor is under different
			 * mode.
			 *
			 * When the descriptor is under blocking mode, the method will block
			 * until enough data is read or when the process receives a POSIX
			 * signal.
			 *
			 * On the other hand, when the descriptor is under non-blocking mode,
			 * the method will save any available data and return immediately. In
			 * this case the method may return without reading anything.
			 */
			void read(Destination&& destination);

			/**
			 * Read data from the file into the buffer. The method will REPEATEDLY
			 * read data from the file and save the result into the buffer. The
			 * method will only return after the buffer is completely filled.
			 */
			void readall(Buffer& buffer);

			/**
			 * Read data from the file into the buffer. The method will REPEATEDLY
			 * read data from the file and save the result into the buffer. The
			 * method will only return after the buffer is completely filled.
			 */
			void readall(Buffer&& buffer);

			/**
			 * Read data from the file into the destination. The method will
			 * REPEATEDLY read data from the file, save the result into the
			 * destination and update the destination counter accordingly. The
			 * method will only return after the destination is completely filled.
			 */
			void readall(Destination& destination);

			/**
			 * Read data from the file into the destination. The method will
			 * REPEATEDLY read data from the file, save the result into the
			 * destination and update the destination counter accordingly. The
			 * method will only return after the destination is completely filled.
			 */
			void readall(Destination&& destination);

			/**
			 * Read data from the file into the destination. The method will
			 * attempt to read data from the file, save the result into the
       * destination and update the destination counter accordingly. The
			 * method will return when:
			 *
			 * 1. Some data is read into the destination; or
			 * 2. The process receives POSIX signal.
			 *
			 * This call is equivalent to calling the `try_readall(Destination, int)`
			 * method with timeout -1.
			 */
			void try_readall(Destination& destination);

			/**
			 * Read data from the file into the destination. The method will
			 * attempt to read data from the file, save the result into the
       * destination and update the destination counter accordingly. The
			 * method will return when:
			 *
			 * 1. Some data is read into the destination; or
			 * 2. The process receives POSIX signal.
			 *
			 * This call is equivalent to calling the `try_readall(Destination, int)`
			 * method with timeout -1.
			 */
			void try_readall(Destination&& destination);

			/**
			 * Read data from the file into the destination. The method will
			 * attempt to read data from the file, save the result into the
       * destination and update the destination counter accordingly. The
			 * method will return when:
			 *
			 * 1. Some data is read into the destination; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
 			 * Note that the timeout accepts 2 special values. The timeout of 0
			 * means no waiting. The timeout of -1 indicates that timeout is
			 * not observed.
			 */
			void try_readall(Destination& destination, int timeout);

			/**
			 * Read data from the file into the destination. The method will
			 * attempt to read data from the file, save the result into the
       * destination and update the destination counter accordingly. The
			 * method will return when:
			 *
			 * 1. Some data is read into the destination; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
 			 * Note that the timeout accepts 2 special values. The timeout of 0
			 * means no waiting. The timeout of -1 indicates that timeout is
			 * not observed.
			 */
			void try_readall(Destination&& destination, int timeout);

			/**
			 * Write data from the buffer to the file. The method will write
			 * the descriptor ONCE with data from the buffer and return the
			 * number of bytes written.
			 */
			std::size_t write(const Buffer& source);

			/**
			 * Write data from the source to the file. The method will write
			 * the descriptor ONCE with data from the source and update the
			 * source counter accordingly.
			 */
			void write(Source& source);

			/**
			 * Write data from the source to the file. The method will write
			 * the descriptor ONCE with data from the source and update the
			 * source counter accordingly.
			 */
			void write(Source&& source);

			/**
			 * Write data from the buffer to the file. The method will REPEATEDLY
			 * write the descriptor with data from the buffer. The method will only
			 * return the buffer is completely written.
			 */
			void writeall(const Buffer& source);

			/**
			 * Write data from the source to the file. The method will REPEATEDLY
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will only return the source is
			 * completely written.
			 */
			void writeall(Source& source);

			/**
			 * Write data from the source to the file. The method will REPEATEDLY
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will only return the source is
			 * completely written.
			 */
			void writeall(Source&& source);

			/**
			 * Write data from the source to the file. The method will attempt to
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will return when:
			 *
			 * 1. Some data is written from the source; or
			 * 2. The process receives posix signal.
			 *
			 * This call is equivalent to calling the `try_writeall(source, int)`
			 * method with timeout -1.
			 */
			void try_writeall(Source& source);

			/**
			 * Write data from the source to the file. The method will attempt to
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will return when:
			 *
			 * 1. Some data is written from the source; or
			 * 2. The process receives posix signal.
			 *
			 * This call is equivalent to calling the `try_writeall(source, int)`
			 * method with timeout -1.
			 */
			void try_writeall(Source&& source);

			/**
			 * Write data from the source to the file. The method will attempt to
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will return when:
			 *
			 * 1. Some data is written from the source; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
 			 * Note that the timeout accepts 2 special values. The timeout of 0
			 * means no waiting. The timeout of -1 indicates that timeout is
			 * not observed.
			 */
			void try_writeall(Source& source, int timeout);

			/**
			 * Write data from the source to the file. The method will attempt to
			 * write the descriptor with data from the source and update the source
			 * counter accordingly. The method will return when:
			 *
			 * 1. Some data is written from the source; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
 			 * Note that the timeout accepts 2 special values. The timeout of 0
			 * means no waiting. The timeout of -1 indicates that timeout is
			 * not observed.
			 */
			void try_writeall(Source&& source, int timeout);

			/**
			 * Truncate the descriptor to the given length.
			 */
			void truncate(std::size_t length);

			/**
			 * Flush the descriptor.
			 */
			void flush();

			File(const File& file) = delete;
			File(File&& file) = delete;
			File& operator=(const File& file) = delete;
			File& operator=(File&& file) = delete;

		private:

			/**
			 * Descriptor of the file.
			 */
			int m_descriptor;

			/**
			 * Whether the descriptor is opened for reading.
			 */
			bool m_readable;

			/**
			 * Whether the descriptor is opened for writing.
			 */
			bool m_writable;

			/**
			 * Whether the descriptor is operating in blocking mode.
			 */
			bool m_blocking;

	};

	/**
	 * Exception thrown when the file reaches its end.
	 */
	class EOFException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception thrown when the file reaches its end while extra data is expected.
	 */
	class PrematureEOFException : public EOFException
	{
		public:
			using EOFException::EOFException;
	};

};


#endif


