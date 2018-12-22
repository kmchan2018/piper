

#include <exception>


#ifndef EXCEPTION_HPP_
#define EXCEPTION_HPP_


namespace Piper
{

	/**
	 * Common exception.
	 */
	class Exception : public std::exception
	{
		public:
			Exception(const char* what, const char* file, unsigned int line) : m_what(what), m_file(file), m_line(line) {}
			const char* what() const noexcept override { return m_what; }
			const char* file() const noexcept { return m_file; }
			unsigned int line() const noexcept { return m_line; }
		private:
			const char* m_what;
			const char* m_file;
			unsigned int m_line;
	};

	/**
	 * Exception reporting that some of the argument is invalid.
	 */
	class InvalidArgumentException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception reporting that the class is in an invalid state.
	 */
	class InvalidStateException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception reporting errors from the operating system.
	 */
	class SystemException : public Exception
	{
		public:
			using Exception::Exception;
	};

	/**
	 * Exception indicating permission error.
	 */
	class PermissionException : public SystemException
	{
		public:
			using SystemException::SystemException;
	};

	/**
	 * Exception for invalid struct alignment.
	 */
	class AlignmentException : public Exception
	{
		public:
			using Exception::Exception;
	};

};


#endif


