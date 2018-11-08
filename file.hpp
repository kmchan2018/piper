

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "exception.hpp"


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
			 * Data class for current execution status of readall operations over file
			 * instances.
			 */
			class ReadAll
			{
				public:
					explicit ReadAll(const File& file, char* buffer, size_t remainder);
				private:
					friend class File;
					const File& m_file;
					char* m_buffer;
					size_t m_done;
					size_t m_remainder;
			};

			/**
			 * Data class for current execution status of writeall operations over file
			 * instances.
			 */
			class WriteAll
			{
				public:
					explicit WriteAll(const File& file, const char* buffer, size_t remainder);
				private:
					friend class File;
					const File& m_file;
					const char* m_buffer;
					size_t m_done;
					size_t m_remainder;
			};

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
			 * Return the descriptor managed by this file instance; return -1 when this
			 * File instances is invalid.
			 */
			int descriptor() const noexcept
			{
				return m_descriptor;
			}

			/**
			 * Read data from the file into the destination buffer. The method will
			 * read data from the file once, deliver them into the buffer, and then
			 * return the number of bytes read from the file.
			 */
			int read(Buffer destination);

			/**
			 * Read data from the file into the destination buffer. The method will
			 * repeatedly read data from the file and deliver them into the buffer
			 * until the whole buffer is filled.
			 */
			void read_all(Buffer destination);

			/**
			 * Read data from the file into the destination buffer. The method will
			 * repeatedly read data from the file and deliver them into the buffer
			 * until the whole buffer is filled.
			 */
			ReadAll read_all_async(Buffer destination);

			/**
			 * Return if the given readall operation is finished or not.
			 */
			bool done(ReadAll& operation);

			/**
			 * Execute the given readall operation.
			 */
			void execute(ReadAll& operation);

			/**
			 * Write data from the source buffer to the file. The method will write
			 * the descriptor once for whatever that is available and return the
			 * number of bytes written to the file.
			 */
			int write(const Buffer& source);

			/**
			 * Write data from the source buffer into the file. The method will
			 * repeatedly fetch data from the buffer and write them into the file
			 * until the whole buffer is processed.
			 */
			void write_all(Buffer source);

			/**
			 * Write data from the source buffer into the file. The method will
			 * repeatedly fetch data from the buffer and write them into the file
			 * until the whole buffer is processed.
			 */
			WriteAll write_all_async(Buffer source);

			/**
			 * Return if the given writeall operation is finished or not.
			 */
			bool done(WriteAll& operation);

			/**
			 * Execute the given writeall operation.
			 */
			void execute(WriteAll& operation);

			/**
			 * Truncate the descriptor to the given length.
			 */
			void truncate(std::size_t length);

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

			File(const File& file) = delete;
			File(File&& file) = delete;
			File& operator=(const File& file) = delete;
			File& operator=(File&& file) = delete;

		private:
			int m_descriptor;
	};

	/**
	 * Exception thrown when the file reaches its end.
	 */
	class EOFException : Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception thrown when the file reaches its end while extra data is expected.
	 */
	class PrematureEOFException : EOFException
	{
		public:
			using EOFException::EOFException;
	};

};


#endif


