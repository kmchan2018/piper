

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

#include "exception.hpp"


#ifndef BUFFER_HPP_
#define BUFFER_HPP_


namespace Piper
{

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
					throw InvalidArgumentException("start should not be null", "buffer.hpp", __LINE__);
				} else if (size == 0) {
					throw InvalidArgumentException("length should not be 0", "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Copy constructor. It creates a new buffer that points to the same
			 * memory region as the old buffer.
			 */
			Buffer(const Buffer& buffer) :
				m_start(buffer.m_start),
				m_size(buffer.m_size)
			{
				// empty
			}

			/**
			 * Move constructor. Since a buffer do not own the memory region,
			 * there are no reason to invalidate the source buffer. Hence the
			 * move constructor is essentially the same as copy constructor.
			 */
			Buffer(const Buffer&& buffer) :
				m_start(buffer.m_start),
				m_size(buffer.m_size)
			{
				// empty
			}

			/**
			 * Move constructor. Since a buffer do not own the memory region,
			 * there are no reason to invalidate the source buffer. Hence the
			 * move constructor is essentially the same as copy constructor.
			 */
			Buffer(Buffer&& buffer) :
				m_start(buffer.m_start),
				m_size(buffer.m_size)
			{
				// empty
			}

			/**
			 * Construct a new buffer backed by the given pointer to struct.
			 */
			template<typename T> explicit Buffer(T* start) :
				m_start(reinterpret_cast<char*>(start)),
				m_size(sizeof(T))
			{
				if (start == nullptr) {
					throw InvalidArgumentException("start should not be null", "buffer.hpp", __LINE__);
				}
			}

			/**
			 * Construct a new buffer backed by the given reference to struct.
			 */
			template<typename T> explicit Buffer(T& start) :
				m_start(reinterpret_cast<char*>(&start)),
				m_size(sizeof(T))
			{
				// empty
			}

			/**
			 * Construct a new buffer backed by the given reference to struct.
			 */
			template<typename T> explicit Buffer(T&& start) :
				m_start(reinterpret_cast<char*>(&start)),
				m_size(sizeof(T))
			{
				// empty
			}

			/**
			 * Copy assignment operator. It updates this buffer to point to
			 * the memory region referred by the other buffer.
			 */
			Buffer& operator=(const Buffer& buffer)
			{
				m_start = buffer.m_start;
				m_size = buffer.m_size;
				return *this;
			}

			/**
			 * Move assignment operator. Since a buffer do not own the memory
			 * region, there are no reason to invalidate the source buffer.
			 * Hence the move assignment operator is essentially the same as
			 * copy assignment operator.
			 */
			Buffer& operator=(Buffer&& buffer)
			{
				m_start = buffer.m_start;
				m_size = buffer.m_size;
				return *this;
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
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
				} else if (start != m_start) {
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
				} else if (size != m_size) {
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
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
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
				} else if (start != m_start) {
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
				} else if (size != m_size) {
					throw AlignmentException("invalid struct type", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid offset", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid offset", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid offset", "buffer.hpp", __LINE__);
				} else if (size > m_size - offset) {
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid offset", "buffer.hpp", __LINE__);
				} else if (size > m_size - offset) {
					throw InvalidArgumentException("invalid size", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid index", "buffer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid index", "buffer.hpp", __LINE__);
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
	 * Copy data from the source buffer into the destination buffer.
	 */
	inline void copy(Buffer& destination, const Buffer& source)
	{
		if (destination.size() >= source.size()) {
			std::memcpy(destination.start(), source.start(), source.size());
		} else {
			throw InvalidArgumentException("source too large", "buffer.hpp", __LINE__);
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
			throw InvalidArgumentException("source too large", "buffer.hpp", __LINE__);
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
			throw InvalidArgumentException("source too large", "buffer.hpp", __LINE__);
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
			throw InvalidArgumentException("source too large", "buffer.hpp", __LINE__);
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


