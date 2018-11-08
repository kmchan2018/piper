

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <limits.h>
#include <time.h>
#include <unistd.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "mmap.hpp"
#include "pipe.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Imports.
	//
	//////////////////////////////////////////////////////////////////////////

	using std::size_t;
	using std::uint32_t;
	using std::uint64_t;
	using std::string;

	using std::atomic_uint32_t;
	using std::atomic_uint64_t;
	using std::atomic_load_explicit;
	using std::atomic_store_explicit;
	using std::atomic_fetch_add_explicit;
	using std::atomic_compare_exchange_strong_explicit;
	using std::memory_order_relaxed;
	using std::memory_order_acquire;
	using std::memory_order_release;
	using std::memory_order_seq_cst;

	//////////////////////////////////////////////////////////////////////////
	//
	// Common session ids.
	//
	//////////////////////////////////////////////////////////////////////////

	const uint32_t INVALID_SESSION_ID = 0;
	const uint32_t INITIAL_SESSION_ID = 1;

	//////////////////////////////////////////////////////////////////////////
	//
	// Backer implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Backer::Backer(const char* path, uint32_t stride, uint32_t capacity, uint32_t period) :
		m_pagesize(::sysconf(_SC_PAGESIZE)),
		m_path(path),
		m_stride(stride),
		m_capacity(capacity),
		m_period(period),
		m_file(path, O_RDWR|O_CREAT|O_EXCL, 0666)
	{
		if (m_stride == 0) {
			throw InvalidArgumentException("stride cannot be zero", "pipe.cpp", __LINE__);
		} else if (m_capacity == 0) {
			throw InvalidArgumentException("capacity cannot be zero", "pipe.cpp", __LINE__);
		} else if (m_period == 0) {
			throw InvalidArgumentException("period cannot be zero", "pipe.cpp", __LINE__);
		}

		Header header;
		header.stride = m_stride;
		header.capacity = m_capacity;
		header.period = m_period;
		header.writes = 0;
		header.tickets = 1;
		header.session = INVALID_SESSION_ID;

		m_file.write_all(Buffer(&header));
		m_file.truncate(m_pagesize + m_stride * m_capacity);
	}

	Backer::Backer(const char* path) :
		m_pagesize(::sysconf(_SC_PAGESIZE)),
		m_path(path),
		m_file(path, O_RDWR)
	{
		Header header;
		m_file.read_all(Buffer(&header));

		if (header.stride == 0) {
			throw InvalidArgumentException("invalid file", "pipe.cpp", __LINE__);
		} else if (header.capacity == 0) {
			throw InvalidArgumentException("invalid file", "pipe.cpp", __LINE__);
		} else if (header.period == 0) {
			throw InvalidArgumentException("invalid file", "pipe.cpp", __LINE__);
		}

		m_stride = header.stride;
		m_capacity = header.capacity;
		m_period = header.period;
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Pipe implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Pipe::Pipe(Backer& backer) :
		m_backer(backer),
		m_header(m_backer.file(), m_backer.header_offset(), m_backer.header_length(), PROT_READ|PROT_WRITE, MAP_SHARED),
		m_buffer(m_backer.file(), m_backer.buffer_offset(), m_backer.buffer_length(), PROT_READ|PROT_WRITE, MAP_SHARED)
	{
	}

	uint64_t Pipe::start() const noexcept
	{
		uint32_t capacity = m_header->capacity;
		uint64_t writes = atomic_load_explicit(&m_header->writes, memory_order_acquire);
		uint64_t start = (writes < capacity ? 0 : writes - capacity);
		return start;
	}

	uint64_t Pipe::until() const noexcept
	{
		uint64_t writes = atomic_load_explicit(&m_header->writes, memory_order_acquire);
		return writes;
	}

	bool Pipe::active() const noexcept
	{
		return atomic_load_explicit(&m_header->session, memory_order_acquire) != INVALID_SESSION_ID;
	}

	const Buffer Pipe::view(uint64_t block) const
	{
		uint32_t capacity = m_header->capacity;
		uint32_t stride = m_header->stride;
		uint64_t writes = atomic_load_explicit(&m_header->writes, memory_order_acquire);
		uint64_t start = (writes < capacity ? 0 : writes - capacity);

		if (block < start) {
			throw InvalidArgumentException("invalid block", "pipe.cpp", __LINE__);
		} else if (block >= writes) {
			throw InvalidArgumentException("invalid block", "pipe.cpp", __LINE__);
		} else {
			uint32_t index = block % capacity;
			uint32_t offset = index * stride;
			return m_buffer.slice(offset, stride);
		}
	}

	uint64_t Pipe::begin()
	{
		uint64_t sid = atomic_fetch_add_explicit(&m_header->tickets, static_cast<uint64_t>(1), memory_order_acquire);
		uint64_t temp = INVALID_SESSION_ID;

		bool result = atomic_compare_exchange_strong_explicit(
			&m_header->session,
			&temp,
			sid,
			memory_order_release,
			memory_order_relaxed);

		if (result) {
			return sid;
		} else {
			throw ConcurrentSessionException("another session underway", "pipe.cpp", __LINE__);
		}
	}

	Buffer Pipe::staging(std::uint64_t sid) const
	{
		if (atomic_load_explicit(&m_header->session, memory_order_acquire) == sid) {
			uint32_t capacity = m_header->capacity;
			uint32_t stride = m_header->stride;
			uint64_t writes = atomic_load_explicit(&m_header->writes, memory_order_acquire);
			uint32_t index = writes % capacity;
			uint32_t offset = index * stride;
			return m_buffer.slice(offset, stride);
		} else {
			throw InvalidArgumentException("invalid session id", "pipe.cpp", __LINE__);
		}
	}

	void Pipe::flush(std::uint64_t sid)
	{
		if (atomic_load_explicit(&m_header->session, memory_order_acquire) == sid) {
			atomic_fetch_add_explicit(&m_header->writes, static_cast<uint64_t>(1), memory_order_release);
		} else {
			throw InvalidArgumentException("invalid session id", "pipe.cpp", __LINE__);
		}
	}

	void Pipe::finish(std::uint64_t sid)
	{
		bool result = atomic_compare_exchange_strong_explicit(
			&m_header->session,
			&sid,
			static_cast<uint64_t>(INVALID_SESSION_ID),
			memory_order_release,
			memory_order_relaxed);

		if (result == false) {
			throw InvalidArgumentException("invalid session id", "pipe.cpp", __LINE__);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Inlet implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Inlet::Inlet(Pipe& pipe) : m_pipe(pipe)
	{
		m_session = m_pipe.begin();
	}

	Inlet::~Inlet()
	{
		m_pipe.finish(m_session);
	}

	Buffer Inlet::staging() const
	{
		return m_pipe.staging(m_session);
	}

	void Inlet::flush()
	{
		return m_pipe.flush(m_session);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Wait operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	inline Outlet::Wait::Wait(const Outlet& outlet) :
		m_outlet(outlet)
	{
		// empty
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Outlet implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Outlet::Outlet(const Pipe& pipe) : m_pipe(pipe)
	{
		uint64_t until = m_pipe.until();
		m_reads = until;
		m_cancel = false;
	}

	bool Outlet::valid() const noexcept
	{
		uint64_t start = m_pipe.start();
		uint64_t until = m_pipe.until();
		return (m_reads >= start && m_reads < until);
	}

	uint32_t Outlet::loss() const noexcept
	{
		uint64_t start = m_pipe.start();
		return (start <= m_reads ? 0 : start - m_reads);
	}

	uint32_t Outlet::available() const noexcept
	{
		uint32_t capacity = m_pipe.capacity();
		uint64_t start = m_pipe.start();
		uint64_t until = m_pipe.until();

		if (m_reads < start) {
			return capacity - 1;
		} else if (m_reads >= start && m_reads < until) {
			return until - m_reads;
		} else {
			return 0;
		}
	}

	const Buffer Outlet::view() const
	{
		uint64_t start = m_pipe.start();
		uint64_t until = m_pipe.until();

		if (m_reads >= start && m_reads < until) {
			return m_pipe.view(m_reads);
		} else {
			throw InvalidStateException("invalid drain", "pipe.cpp", __LINE__);
		}
	}

	void Outlet::wait()
	{
		Outlet::Wait operation = wait_async();
		while (done(operation) == false) {
			execute(operation);
		}
	}

	Outlet::Wait Outlet::wait_async()
	{
		return Outlet::Wait(*this);
	}

	bool Outlet::done(Outlet::Wait& operation)
	{
		if (this == &operation.m_outlet) {
			return m_pipe.until() > m_reads;
		} else {
			throw InvalidArgumentException("invalid operation", "pipe.cpp", __LINE__);
		}
	}

	void Outlet::execute(Outlet::Wait& operation)
	{
		if (this == &operation.m_outlet) {
			if (m_pipe.until() <= m_reads) {
				struct timespec wait;
				wait.tv_sec = 0;
				wait.tv_nsec = m_pipe.period() * 500000L * (m_pipe.active() ? 1L : 1L);
				::nanosleep(&wait, NULL);
			}
		} else {
			throw InvalidArgumentException("invalid operation", "pipe.cpp", __LINE__);
		}
	}

	void Outlet::drain()
	{
		uint64_t until = m_pipe.until();

		if (m_reads < until) {
			m_reads++;
		} else {
			throw InvalidStateException("invalid drain", "pipe.cpp", __LINE__);
		}
	}

	void Outlet::recover(uint32_t target)
	{
		uint32_t window = m_pipe.capacity() - 1;
		uint64_t start = m_pipe.start();
		uint64_t until = m_pipe.until();

		if (start <= m_reads) {
			throw InvalidStateException("recovery not needed", "pipe.cpp", __LINE__);
		} else if (target == 0) {
			throw InvalidArgumentException("invalid target", "pipe.cpp", __LINE__);
		} else if (target > window) {
			throw InvalidArgumentException("invalid target", "pipe.cpp", __LINE__);
		} else {	
			m_reads = until - target;
		}
	}

};


