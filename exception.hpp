

#include <cstring>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>


#ifndef EXCEPTION_HPP_
#define EXCEPTION_HPP_


namespace Support::Exception
{

	/**
	 * Location data class. It contains information about a location in the
	 * source code including the file name and line number in the file. The
	 * class is designed to be mixed into another class to annotate other 
	 * objects, including exceptions, with source location.
	 */
	class Location
	{
		public:
			Location() : m_file(nullptr), m_line(0) {}
			Location(const char* file, unsigned int line) : m_file(file), m_line(line) { if (file == nullptr) throw std::invalid_argument("file cannot be null"); }
			virtual ~Location() {}
			bool valid() const noexcept { return m_file != nullptr; }
			const char* file() const noexcept { return (m_file != nullptr ? m_file : "unknown"); }
			unsigned int line() const noexcept { return m_line; }
		private:
			const char* m_file;
			unsigned int m_line;
	};

	/**
	 * Template for root exception at the root of the exception chain. The
	 * class inherits from both the original exception class as well as the
	 * location class.
	 */
	template<typename Exception>
	class RootException : public Exception, public Location
	{
		public:
			RootException(Exception&& exception, Location&& location) : Exception{exception}, Location{location} {}
	};

	/**
	 * Template for wrapper exception at the middle of the exception chain. The
	 * class inherits from both the original exception class as well as both 
	 * location and nested_exception classes.
	 */
	template<typename Exception>
	class WrapperException : public Exception, public Location, public std::nested_exception
	{
		public:
			WrapperException(Exception&& exception, Location&& location) : Exception{exception}, Location{location}, std::nested_exception{} {}
			WrapperException(Exception&& exception, Location&& location, std::exception_ptr wrapped) : Exception{exception}, Location{location}, std::nested_exception{wrapped} {}
	};

	/**
	 * Throws the initial exception in an exception chain. The input exception
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * input exception with location attached via multiple inheritance. In case
	 * the input exception does not match the requirement, the input will be
	 * thrown directly.
	 *
	 * This override implements the case where the requirements are met.
	 */
	template<typename Exception, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception>::type>::value,
		bool, void>::type = true>
	[[ noreturn ]] inline void start(Exception&& exception, const char* file, unsigned int line)
	{
		throw RootException<typename std::decay<Exception>::type>(std::forward<Exception>(exception), Location(file, line));
	}

	/**
	 * Throws the initial exception in an exception chain. The input exception
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * input exception with location attached via multiple inheritance. In case
	 * the input exception does not match the requirement, the input will be
	 * thrown directly.
	 *
	 * This override implements the case where the requirements are not met.
	 */
	template<typename Exception, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception>::type>::value,
		void, bool>::type = true>
	[[ noreturn ]] inline void start(Exception&& exception, const char* file, unsigned int line)
	{
		throw std::forward<Exception>(exception);
	}

	/**
	 * Throws the initial exceptions in an exception chain. The input exceptions
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * second input exception with location and nested_exception attached via
	 * multiple inheritance. The exception will in turn nest the a copy of the
	 * first input exception with location attached via multiple inheritance.
	 * In case any input exception does not match the requirement, the first
	 * input exception will be thrown directly.
	 *
	 * This override implements the case where the requirements are met.
	 */
	template<typename Exception1, typename Exception2, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception1>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception1>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception1>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception1>::type>::value &&
		std::is_base_of<std::exception, typename std::decay<Exception2>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception2>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception2>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception2>::type>::value,
		bool, void>::type = true>
	[[ noreturn ]] inline void start(Exception1&& exception1, Exception2&& exception2, const char* file, unsigned int line)
	{
		try {
			throw RootException<typename std::decay<Exception1>::type>(std::forward<Exception1>(exception1), Location(file, line));
		} catch (Exception1& ex) {
			throw WrapperException<typename std::decay<Exception2>::type>(std::forward<Exception2>(exception2), Location(file, line));
		}
	}

	/**
	 * Throws the initial exceptions in an exception chain. The input exceptions
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * second input exception with location and nested_exception attached via
	 * multiple inheritance. The exception will in turn nest the a copy of the
	 * first input exception with location attached via multiple inheritance.
	 * In case any input exception does not match the requirement, the first
	 * input exception will be thrown directly.
	 *
	 * This override implements the case where the requirements are not met.
	 */
	template<typename Exception1, typename Exception2, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception1>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception1>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception1>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception1>::type>::value &&
		std::is_base_of<std::exception, typename std::decay<Exception2>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception2>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception2>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception2>::type>::value,
		void, bool>::type = true>
	[[ noreturn ]] inline void start(Exception1&& exception1, Exception2&& exception2, const char* file, unsigned int line)
	{
		throw std::forward<Exception1>(exception1);
	}

	/**
	 * Throws the following exception in an exception chain. The input exception
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * input exception with location and nested_exception attached via multiple
	 * inheritance. In case the input exception does not match the requirement,
	 * the input will be thrown directly.
	 *
	 * This override implements the case where the requirements are met.
	 */
	template<typename Exception, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception>::type>::value,
		bool, void>::type = true>
	[[ noreturn ]] inline void chain(Exception&& exception, const char* file, unsigned int line)
	{
		throw WrapperException<typename std::decay<Exception>::type>(std::forward<Exception>(exception), Location(file, line));
	}

	/**
	 * Throws the following exception in an exception chain. The input exception
	 * should be an copy-constructible exception object without location or
	 * nested_exception attachments. The thrown exception will be a copy of the
	 * input exception with location and nested_exception attached via multiple
	 * inheritance. In case the input exception does not match the requirement,
	 * the input will be thrown directly.
	 *
	 * This override implements the case where the requirements are not met.
	 */
	template<typename Exception, typename std::conditional<
		std::is_base_of<std::exception, typename std::decay<Exception>::type>::value &&
		std::is_copy_constructible<typename std::decay<Exception>::type>::value &&
		!std::is_base_of<Location, typename std::decay<Exception>::type>::value &&
		!std::is_base_of<std::nested_exception, typename std::decay<Exception>::type>::value,
		void, bool>::type = true>
	[[ noreturn ]] inline void chain(Exception&& exception, const char* file, unsigned int line)
	{
		throw std::forward<Exception>(exception);
	}

	/**
	 * Extract the location from the given exception. If the given exception does
	 * not contain location data, a dummy, invalid location instance will be
	 * returned.
	 */
	inline const Location& location(const std::exception& exception)
	{
		static const Location UNKNOWN;
		try {
			const Location& location = dynamic_cast<const Location&>(exception);
			return location;
		} catch (std::bad_cast& ex) {
			return UNKNOWN;
		}
	}

	/**
	 * Extract the exception pointer to the wrapped exception from the given
	 * exception. If the given exception does not nest another exception, a
	 * default-constructed exception pointer (which does not point to any
	 * exception object) will be returned.
	 */
	inline const std::exception_ptr cause(const std::exception& exception)
	{
		try {
			const std::nested_exception& nested = dynamic_cast<const std::nested_exception&>(exception);
			return nested.nested_ptr();
		} catch (std::bad_cast& ex) {
			return std::exception_ptr();
		}
	}

};


#endif


