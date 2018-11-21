

#include <cstddef>
#include <initializer_list>
#include <utility>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"


#ifndef MMAP_HPP_
#define MMAP_HPP_


namespace Piper
{

	/**
	 * MappedStruct represents a structure on mapped memory.
	 */
	template<typename T> class MappedStruct
	{
		public:

			/**
			 * Construct a mapped struct instance from its components. Throws invalid
			 * argument exception when either pointer and/or size is invalid.
			 */
			explicit inline MappedStruct(void* pointer, std::size_t size) :
				m_pointer(pointer),
				m_size(size)
			{
				if (nullptr == pointer) {
					throw InvalidArgumentException("invalid pointer", "mmap.hpp", __LINE__);
				} else if (0 == size) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Construct a mapped struct instance by mmap syscall. Throws invalid
			 * argument exception when any of the arguments is invalid.
			 */
			explicit inline MappedStruct(const File& file, std::size_t offset, std::size_t size, int prot, int flags)
			{
				void* pointer = nullptr;

				if (0 == size) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				} else if (0 == (flags & MAP_SHARED) && 0 == (flags & MAP_PRIVATE)) {
					throw InvalidArgumentException("invalid flags", "mmap.hpp", __LINE__);
				} else if (0 != (flags & MAP_SHARED) && 0 != (flags & MAP_PRIVATE)) {
					throw InvalidArgumentException("invalid flags", "mmap.hpp", __LINE__);
				} else if ((pointer = ::mmap(0, size, prot, flags, file.descriptor(), offset)) == MAP_FAILED) {
					switch (errno) {
						case EACCES: throw InvalidArgumentException("invalid file type", "mmap.hpp", __LINE__);
						case EBADF: throw InvalidArgumentException("invalid file descriptor", "mmap.hpp", __LINE__);
						case EINVAL: throw InvalidArgumentException("invalid offset, size, flags or prot", "mmap.hpp", __LINE__);
						default: throw SystemException("cannot map memory", "mmap.hpp", __LINE__);
					}
				} else {
					m_pointer = static_cast<T*>(pointer);
					m_size = size;
				}
			}

			/**
			 * Destruct this mapped struct instance by munmap the shared memory
			 * region.
			 */
			inline ~MappedStruct()
			{
				::munmap(m_pointer, m_size);
			}

			/**
			 * Return the pointer to the mapped memory region.
			 */
			inline T* pointer() const noexcept
			{
				return m_pointer;
			}

			/**
			 * Return the pointer to the mapped memory region.
			 */
			inline T* get() const noexcept
			{
				return m_pointer;
			}

			/**
			 * Return the size of the mapped memory region.
			 */
			inline std::size_t size() const noexcept
			{
				return m_size;
			}

			/**
			 * Instantiate the shared memory region by calling the constructor of 
			 * the embedded struct. Throws exception when this mapped struct instance
			 * is invalid.
			 */
			inline void bootstrap()
			{
				new(static_cast<void*>(m_pointer)) T();
			}

			/**
			 * Instantiate the shared memory region by calling the constructor of 
			 * the embedded struct. Throws exception when this mapped struct instance
			 * is invalid.
			 */
			inline void bootstrap(std::initializer_list<T> ilist)
			{
				new(static_cast<void*>(m_pointer)) T(ilist);
			}

			/**
			 * Override the member of pointer operator on this mapped struct instance.
			 * Throws exception when this mapped struct instance is invalid.
			 */
			inline T* operator->() const noexcept
			{
				return m_pointer;
			}

			/**
			 * Override the indirection operator on this mapped struct instance. Throws
			 * exception when this mapped struct instance is invalid.
			 */
			inline T& operator*() const noexcept
			{
				return *m_pointer;
			}

			MappedStruct(const MappedStruct<T>& map) = delete;
			MappedStruct(MappedStruct<T>&& map) = delete;
			MappedStruct& operator=(const MappedStruct<T>& map) = delete;
			MappedStruct& operator=(MappedStruct<T>&& map) = delete;

		private:
			T* m_pointer;
			std::size_t m_size;
	};


	/**
	 * MappedBuffer represents a buffer on mapped memory.
	 */
	class MappedBuffer {
		public:

			/**
			 * Construct a mapped buffer instance from its components. Throws invalid
			 * argument exception when either start pointer and/or size is invalid.
			 */
			explicit inline MappedBuffer(char* start, std::size_t size) :
				m_start(start),
				m_size(size)
			{
				if (start == nullptr) {
					throw InvalidArgumentException("invalid start", "mmap.hpp", __LINE__);
				} else if (size == 0) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Construct a mapped buffer instance by mmap syscall. Throws invalid
			 * argument exception when any of the arguments is invalid.
			 */
			explicit inline MappedBuffer(const File& file, std::size_t offset, std::size_t size, int prot, int flags)
			{
				void* start = nullptr;

				if (0 == size) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				} else if (0 == (flags & MAP_SHARED) && 0 == (flags & MAP_PRIVATE)) {
					throw InvalidArgumentException("invalid flags", "mmap.hpp", __LINE__);
				} else if (0 != (flags & MAP_SHARED) && 0 != (flags & MAP_PRIVATE)) {
					throw InvalidArgumentException("invalid flags", "mmap.hpp", __LINE__);
				} else if ((start = ::mmap(0, size, prot, flags, file.descriptor(), offset)) == MAP_FAILED) {
					switch (errno) {
						case EACCES: throw InvalidArgumentException("invalid file type", "mmap.hpp", __LINE__);
						case EBADF: throw InvalidArgumentException("invalid file descriptor", "mmap.hpp", __LINE__);
						case EINVAL: throw InvalidArgumentException("invalid offset, size, flags or prot", "mmap.hpp", __LINE__);
						default: throw SystemException("cannot map memory", "mmap.hpp", __LINE__);
					}
				} else {
					m_start = static_cast<char*>(start);
					m_size = size;
				}
			}

			/**
			 * Destruct this mapped buffer instance by munmap the shared memory
			 * region.
			 */
			inline ~MappedBuffer()
			{
				::munmap(m_start, m_size);
			}

			/**
			 * Returns the size of this buffer.
			 */
			inline std::size_t size() const noexcept
			{
				return m_size;
			}

			/**
			 * Returns a pointer to the start of this buffer.
			 */
			inline const char* start() const noexcept
			{
				return m_start;
			}

			/**
			 * Returns a pointer to the start of this buffer.
			 */
			inline char* start() noexcept
			{
				return m_start;
			}

			/**
			 * Returns a pointer to a specific offset of the memory region. Throws
			 * invalid argument exception when the offset extends past the end of
			 * this buffer.
			 */
			inline const char* at(std::size_t offset) const
			{
				if (offset < m_size) {
					return m_start + offset;
				} else {
					throw InvalidArgumentException("invalid offset", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a pointer to a specific offset of the memory region. Throws
			 * invalid argument exception when the offset extends past the end of
			 * this buffer.
			 */
			inline char* at(std::size_t offset)
			{
				if (offset < m_size) {
					return m_start + offset;
				} else {
					throw InvalidArgumentException("invalid offset", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the first n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			inline const Buffer head(std::size_t size) const
			{
				if (size <= m_size) {
					return Buffer(m_start, size);
				} else {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the first n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			inline Buffer head(std::size_t size)
			{
				if (size <= m_size) {
					return Buffer(m_start, size);
				} else {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the last n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			inline const Buffer tail(std::size_t size) const
			{
				if (size <= m_size) {
					return Buffer(m_start + (m_size - size), size);
				} else {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer representing the last n bytes of this buffer. Throws
			 * exception when n is larger than the size of this buffer.
			 */
			inline Buffer tail(std::size_t size)
			{
				if (size <= m_size) {
					return Buffer(m_start + (m_size - size), size);
				} else {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				}
			}

			/**
			 * Returns a buffer that starts from the specific offset of this buffer
			 * and with the given size. Throws exception when the slice extends
			 * past the end of this buffer.
			 */
			inline const Buffer slice(std::size_t offset, std::size_t size) const
			{
				if (offset >= m_size) {
					throw InvalidArgumentException("invalid offset", "mmap.hpp", __LINE__);
				} else if (size > m_size - offset) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				} else {
					return Buffer(m_start + offset, size);
				}
			}

			/**
			 * Returns a buffer that starts from the specific offset of this buffer
			 * and with the given size. Throws exception when the slice extends
			 * past the end of this buffer.
			 */
			inline Buffer slice(std::size_t offset, std::size_t size)
			{
				if (offset >= m_size) {
					throw InvalidArgumentException("invalid offset", "mmap.hpp", __LINE__);
				} else if (size > m_size - offset) {
					throw InvalidArgumentException("invalid size", "mmap.hpp", __LINE__);
				} else {
					return Buffer(m_start + offset, size);
				}
			}

			MappedBuffer(const MappedBuffer& map) = delete;
			MappedBuffer(MappedBuffer&& map) = delete;
			MappedBuffer& operator=(const MappedBuffer& map) = delete;
			MappedBuffer& operator=(MappedBuffer&& map) = delete;

		private:
			char* m_start;
			std::size_t m_size;
	};

};


#endif


