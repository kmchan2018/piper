

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "exception.hpp"


#ifndef BUFFER_HPP_
#define BUFFER_HPP_


namespace Piper
{

	using Support::Exception::start;

	/**
	 * Buffer is a value class that references a memory region. A buffer consists
	 * of two components, including the pointer to the start of the region, and
	 * the size of the region in bytes.
	 *
	 * Note the following points about the class:
	 *
	 * Buffer does not assume ownership to the memory region and therefore is not
	 * responsible for freeing the memory region. Also, it is possible that
	 * multiple buffer instance to refer to a single region. 
	 *
	 * Const buffers are considered a read only view to the given memory region.
	 * A onst buffer will only return pointers to const data; moreover, the
	 * `head`, `tail` and `slice` operations will only return const buffers.
	 */
	class Buffer
	{
		public:

			/**
			 * Construct a new buffer from its components. Throws invalid argument
			 * exception when start and/or size is invalid.
			 */
			explicit Buffer(char* start, std::size_t size) : m_start(start), m_size(size)
			{
				if (start == nullptr) {
					Support::Exception::start(std::invalid_argument("[Piper::Buffer::Buffer] start should not be null"), "buffer.hpp", __LINE__);
				} else if (size == 0) {
					Support::Exception::start(std::invalid_argument("[Piper::Buffer::Buffer] length should not be 0"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Construct a new buffer backed by the given pointer to struct.
			 */
			template<typename T, typename std::conditional<std::is_base_of<Buffer,T>::value, void, bool>::type = false>
			explicit Buffer(T* start) :
				m_start(reinterpret_cast<char*>(start)),
				m_size(sizeof(T))
			{
				if (start == nullptr) {
					Support::Exception::start(std::invalid_argument("[Piper::Buffer::Buffer] start should not be null"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Construct a new buffer backed by the given reference to struct.
			 */
			template<typename T, typename std::conditional<std::is_base_of<Buffer,T>::value, void, bool>::type = false>
			explicit Buffer(T& start) :
				m_start(reinterpret_cast<char*>(std::addressof(start))),
				m_size(sizeof(T))
			{
				// empty
			}

			/**
			 * Returns the size of this buffer.
			 */
			std::size_t size() const noexcept
			{
				return m_size;
			}

			/**
			 * Returns a pointer to the start of this buffer.
			 */
			const char* start() const noexcept
			{
				return m_start;
			}

			/**
			 * Returns a pointer to the start of this buffer.
			 */
			char* start() noexcept
			{
				return m_start;
			}

			/**
			 * Cast the buffer as a pointer to struct.
			 */
			template<typename T> const T* to_struct_pointer() const
			{
				void* start = m_start;
				std::size_t size = m_size;

				if (std::align(alignof(T), sizeof(T), start, size) == nullptr) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else if (start != m_start) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else if (size != m_size) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else {
					return reinterpret_cast<T*>(m_start);
				}
			}

			/**
			 * Cast the buffer as a pointer to struct.
			 */
			template<typename T> T* to_struct_pointer()
			{
				void* start = m_start;
				std::size_t size = m_size;

				if (std::align(alignof(T), sizeof(T), start, size) == nullptr) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else if (start != m_start) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else if (size != m_size) {
					Support::Exception::start(std::logic_error("[Piper::Buffer::to_struct_pointer] Cannot cast buffer to struct due to misalignment"), "buffer.hpp", __LINE__);
				} else {
					return reinterpret_cast<T*>(m_start);
				}
			}

			/**
			 * Cast the buffer as a reference to struct.
			 */
			template<typename T> const T& to_struct_reference() const
			{
				return *(to_struct_pointer<T>());
			}

			/**
			 * Cast the buffer as a reference to struct.
			 */
			template<typename T> T& to_struct_reference()
			{
				return *(to_struct_pointer<T>());
			}

			/**
			 * Returns a pointer to a specific offset of the memory region. Throws
			 * invalid argument exception when the offset extends past the end of
			 * this buffer.
			 */
			const char* at(std::size_t offset) const
			{
				if (offset < m_size) {
					return m_start + offset;
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::at] offset should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a pointer to a specific offset of the memory region. Throws
			 * invalid argument exception when the offset extends past the end of
			 * this buffer.
			 */
			char* at(std::size_t offset)
			{
				if (offset < m_size) {
					return m_start + offset;
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::at] offset should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the first n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			const Buffer head(std::size_t size) const
			{
				if (size <= m_size) {
					return Buffer(m_start, size);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::head] size should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the first n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			Buffer head(std::size_t size)
			{
				if (size <= m_size) {
					return Buffer(m_start, size);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::head] size should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the last n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			const Buffer tail(std::size_t size) const
			{
				if (size <= m_size) {
					return Buffer(m_start + (m_size - size), size);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::tail] size should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the last n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			Buffer tail(std::size_t size)
			{
				if (size <= m_size) {
					return Buffer(m_start + (m_size - size), size);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::tail] size should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer that starts from the specific offset of this buffer
			 * and with the given size. Throws exception when the slice extends
			 * past the end of this buffer.
			 */
			const Buffer slice(std::size_t offset, std::size_t size) const
			{
				if (offset >= m_size) {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::slice] offset should not exceed buffer size"), "buffer.hpp", __LINE__);
				} else if (size > m_size - offset) {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::slice] size should not exceed available space in the buffer after the given offset"), "buffer.hpp", __LINE__);
				} else {
					return Buffer(m_start + offset, size);
				}
			}

			/**
			 * Returns a buffer that starts from the specific offset of this buffer
			 * and with the given size. Throws exception when the slice extends
			 * past the end of this buffer.
			 */
			Buffer slice(std::size_t offset, std::size_t size)
			{
				if (offset >= m_size) {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::slice] offset should not exceed buffer size"), "buffer.hpp", __LINE__);
				} else if (size > m_size - offset) {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::slice] size should not exceed available space in the buffer after the given offset"), "buffer.hpp", __LINE__);
				} else {
					return Buffer(m_start + offset, size);
				}
			}

			/**
			 * Index operation.
			 */
			const char& operator[](std::size_t index) const
			{
				if (index < m_size) {
					return *(m_start + index);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::operator[]] index should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Index operation.
			 */
			char& operator[](std::size_t index)
			{
				if (index < m_size) {
					return *(m_start + index);
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Buffer::operator[]] index should not exceed buffer size"), "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Swap the content of this buffer instance with another buffer instance.
			 * This method is used to support std::swap operation over buffer
			 * instances.
			 */
			void swap(Buffer& other) noexcept
			{
				std::swap(m_start, other.m_start);
				std::swap(m_size, other.m_size);
			}

		private:
			char* m_start;
			std::size_t m_size;

	};

	/**
	 * Source implements a buffer wrapper that represents a source where data
	 * can be read from.
	 *
	 * Essentially, a source contains 2 components. It includes a buffer where
	 * data can be found, and a counter indicating the amount of pending data.
	 */
	class Source
	{
		public:

			/**
			 * Construct a new source.
			 */
			explicit Source(const Buffer& buffer) : m_buffer(buffer), m_remainder(buffer.size())
			{
				// do nothing
			}

			/**
			 * Return the buffer where the source reads from.
			 */
			const Buffer& buffer() const noexcept
			{
				return m_buffer;
			}

			/**
			 * Return the number of bytes that are available for reading.
			 */
			unsigned int total() const noexcept
			{
				return m_buffer.size();
			}

			/**
			 * Return the number of bytes that are already read.
			 */
			unsigned int read() const noexcept
			{
				return m_buffer.size() - m_remainder;
			}

			/**
			 * Return the number of bytes in the buffer to be read.
			 */
			unsigned int remainder() const noexcept
			{
				return m_remainder;
			}

			/**
			 * Return a buffer containing the unread data. Throws exception when
			 * there are no data left.
			 */
			const Buffer data()
			{
				return m_buffer.tail(m_remainder);
			}

			/**
			 * Consume the given amount of data as processed.
			 */
			void consume(unsigned int consumed)
			{
				if (consumed <= m_remainder) {
					m_remainder -= consumed;
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Source::consume] consumed should not exceed remainder size"), "buffer.hpp", __LINE__);
				}
			}

		private:
			const Buffer& m_buffer;
			unsigned int m_remainder;
	};

	/**
	 * Destination implements a buffer wrapper that represents a destination
	 * where data can be written to.
	 *
	 * Essentially, a destination contains 2 components. It includes a buffer
	 * where data can be stored, and a counter indicating the amount of unused
	 * space.
	 */
	class Destination
	{
		public:

			/**
			 * Construct a new destination.
			 */
			explicit Destination(Buffer& buffer) : m_buffer(buffer), m_remainder(buffer.size())
			{
				// do nothing
			}

			/**
			 * Return the buffer where the transfer processes.
			 */
			Buffer& buffer() const noexcept
			{
				return m_buffer;
			}

			/**
			 * Return the number of bytes that are available for processing.
			 */
			unsigned int total() const noexcept
			{
				return m_buffer.size();
			}

			/**
			 * Return the number of bytes that are already written.
			 */
			unsigned int written() const noexcept
			{
				return m_buffer.size() - m_remainder;
			}

			/**
			 * Return the number of bytes in the buffer to be written.
			 */
			unsigned int remainder() const noexcept
			{
				return m_remainder;
			}

			/**
			 * Return the slice to the buffer where the data awaits processing.
			 * Throws exception when there are no data left.
			 */
			Buffer data()
			{
				return m_buffer.tail(m_remainder);
			}

			/**
			 * Consume the given amount of data as processed.
			 */
			void consume(unsigned int consumed)
			{
				if (consumed <= m_remainder) {
					m_remainder -= consumed;
				} else {
					Support::Exception::start(std::out_of_range("[Piper::Destination::consume] consumed should not exceed remainder size"), "buffer.hpp", __LINE__);
				}
			}

		private:
			Buffer& m_buffer;
			unsigned int m_remainder;
	};

	/**
	 * Copy data from the source buffer into the destination buffer.
	 */
	inline void copy(Buffer& destination, const Buffer& source)
	{
		if (destination.size() >= source.size()) {
			std::memcpy(destination.start(), source.start(), source.size());
		} else {
			Support::Exception::start(std::invalid_argument("[Piper::copy] source too large"), "buffer.hpp", __LINE__);
		}
	}

	/**
	 * Copy data from the source variable into the destination buffer.
	 */
	template<typename T> inline void copy(Buffer& destination, const T& source)
	{
		if (destination.size() >= sizeof(T)) {
			std::memcpy(destination.start(), &source, sizeof(T));
		} else {
			Support::Exception::start(std::invalid_argument("[Piper::copy] source too large"), "buffer.hpp", __LINE__);
		}
	}

	/**
	 * Copy data from the source variable into the destination buffer.
	 */
	template<typename T> inline void copy(Buffer& destination, const T* source)
	{
		if (destination.size() >= sizeof(T)) {
			std::memcpy(destination.start(), reinterpret_cast<void*>(source), sizeof(T));
		} else {
			Support::Exception::start(std::invalid_argument("[Piper::copy] source too large"), "buffer.hpp", __LINE__);
		}
	}

	/**
	 * Copy data from the source buffer into the destination variable.
	 */
	template<typename T> inline void copy(T* destination, const Buffer& source)
	{
		if (sizeof(T) >= source.size()) {
			std::memcpy(reinterpret_cast<void*>(destination), source.start(), source.size());
		} else {
			Support::Exception::start(std::invalid_argument("[Piper::copy] source too large"), "buffer.hpp", __LINE__);
		}
	}

	/**
	 * Specialize the swap call for buffer instances.
	 */
	inline void swap(Buffer &a, Buffer &b) noexcept
	{
		return a.swap(b);
	}

};


#endif


