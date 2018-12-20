

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "transport.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Common session ids.
	//
	//////////////////////////////////////////////////////////////////////////

	const Transport::Session INVALID_SESSION_ID = 0;
	const Transport::Session INITIAL_SESSION_ID = 1;

	//////////////////////////////////////////////////////////////////////////
	//
	// Helper functions.
	//
	//////////////////////////////////////////////////////////////////////////

	static inline std::size_t align(std::size_t offset, std::size_t align)
	{
		std::size_t extra = offset % align;
		return offset + (extra > 0 ? align - extra : 0);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Backer implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Backer::Backer(const char* path, const Buffer& metadata, const std::vector<std::size_t> components, unsigned int slots, unsigned int mode) :
		m_path(path),
		m_file(path, O_RDWR|O_CREAT|O_EXCL, mode),
		m_slot_count(slots),
		m_component_count(components.size()),
		m_page_size(::sysconf(_SC_PAGESIZE)),
		m_header_offset(0),
		m_header_size(sizeof(Header)),
		m_metadata_offset(m_page_size),
		m_metadata_size(metadata.size()),
		m_total_size(align(m_metadata_offset + m_metadata_size, m_page_size))
	{
		if (m_slot_count < 2) {
			throw InvalidArgumentException("invalid slot count", "transport.cpp", __LINE__);
		} else if (m_slot_count > UINT32_MAX) {
			throw InvalidArgumentException("invalid slot count", "transport.cpp", __LINE__);
		} else if (m_component_count <= 0) {
			throw InvalidArgumentException("invalid component count", "transport.cpp", __LINE__);
		} else if (m_component_count > MAX_COMPONENT_COUNT) {
			throw InvalidArgumentException("invalid component count", "transport.cpp", __LINE__);
		} else if (m_metadata_size == 0) {
			throw InvalidArgumentException("invalid metadata size", "transport.cpp", __LINE__);
		} else if (m_metadata_size > UINT32_MAX) {
			throw InvalidArgumentException("invalid metadata size", "transport.cpp", __LINE__);
		}

		Header header;
		header.slot_count = m_slot_count;
		header.component_count = m_component_count;
		header.metadata_size = m_metadata_size;
		header.writes = 0;
		header.tickets = 1;
		header.session = INVALID_SESSION_ID;

		for (unsigned int i = 0; i < m_component_count; i++) {
			std::size_t component_size = components[i];
			if (component_size == 0) {
				throw InvalidArgumentException("invalid component size", "transport.cpp", __LINE__);
			} else if (component_size > UINT32_MAX) {
				throw InvalidArgumentException("invalid component size", "transport.cpp", __LINE__);
			} else {
				m_component_offsets[i] = m_total_size;
				m_component_sizes[i] = header.component_sizes[i] = component_size;
				m_total_size = align(m_component_offsets[i] + m_component_sizes[i] * m_slot_count, m_page_size);
			}
		}

		for (unsigned int i = m_component_count; i < MAX_COMPONENT_COUNT; i++) {
			m_component_offsets[i] = 0;
			m_component_sizes[i] = header.component_sizes[i] = 0;
		}

		m_file.truncate(m_total_size);
		m_file.seek(m_header_offset, SEEK_SET);
		m_file.writeall(Buffer(&header));
		m_file.seek(m_metadata_offset, SEEK_SET);
		m_file.writeall(metadata);
		m_file.flush();
	}

	Backer::Backer(const char* path) :
		m_path(path),
		m_file(path, O_RDWR),
		m_slot_count(0),
		m_component_count(0),
		m_page_size(::sysconf(_SC_PAGESIZE)),
		m_header_offset(0),
		m_header_size(sizeof(Header)),
		m_metadata_offset(m_page_size),
		m_metadata_size(0),
		m_total_size(m_page_size)
	{
		Header header;
		m_file.readall(Buffer(&header));

		if (header.slot_count < 2) {
			throw InvalidArgumentException("invalid channel file", "transport.cpp", __LINE__);
		} else if (header.component_count == 0) {
			throw InvalidArgumentException("invalid channel file", "transport.cpp", __LINE__);
		} else if (header.component_count > MAX_COMPONENT_COUNT) {
			throw InvalidArgumentException("invalid channel file", "transport.cpp", __LINE__);
		} else if (header.metadata_size == 0) {
			throw InvalidArgumentException("invalid channel file", "transport.cpp", __LINE__);
		}

		m_slot_count = header.slot_count;
		m_component_count = header.component_count;
		m_metadata_size = header.metadata_size;
		m_total_size = align(m_metadata_offset + m_metadata_size, m_page_size);

		for (unsigned int i = 0; i < m_component_count; i++) {
			std::size_t component_size = header.component_sizes[i];
			if (component_size == 0) {
				throw InvalidArgumentException("invalid channel file", "transport.cpp", __LINE__);
			} else {
				m_component_offsets[i] = m_total_size;
				m_component_sizes[i] = component_size;
				m_total_size = align(m_component_offsets[i] + m_component_sizes[i] * m_slot_count, m_page_size);
			}
		}

		for (unsigned int i = m_component_count; i < MAX_COMPONENT_COUNT; i++) {
			m_component_offsets[i] = 0;
			m_component_sizes[i] = 0;
		}
	}

	inline std::size_t Backer::component_offset(unsigned int slot, unsigned int component) const
	{
		if (slot >= m_slot_count) {
			throw InvalidArgumentException("invalid slot", "transport.cpp", __LINE__);
		} else if (component >= m_component_count) {
			throw InvalidArgumentException("invalid component", "transport.cpp", __LINE__);
		} else {
			return m_component_offsets[component] + slot * m_component_sizes[component];
		}
	}

	std::size_t Backer::component_size(unsigned int component) const
	{
		if (component >= m_component_count) {
			throw InvalidArgumentException("invalid component", "transport.cpp", __LINE__);
		} else {
			return m_component_sizes[component];
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Medium implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Medium::Medium(Backer& backer) :
		m_backer(backer),
		m_size(m_backer.total_size()),
		m_pointer(reinterpret_cast<char*>(::mmap(nullptr, m_size, PROT_READ|PROT_WRITE, MAP_SHARED, m_backer.file().descriptor(), 0)))
	{
		if (m_pointer == MAP_FAILED) {
			switch (errno) {
				case EACCES: throw InvalidArgumentException("invalid file type", "transport.cpp", __LINE__);
				case EBADF: throw InvalidArgumentException("invalid file descriptor", "transport.cpp", __LINE__);
				case EINVAL: throw InvalidArgumentException("invalid offset, size, flags or prot", "transport.cpp", __LINE__);
				default: throw SystemException("cannot map memory", "transport.cpp", __LINE__);
			}
		}
	}

	Medium::~Medium()
	{
		::munmap(m_pointer, m_size);
	}

	const Medium::WriteCounter& Medium::writes() const noexcept
	{
		WriteCounter* writes = reinterpret_cast<WriteCounter*>(m_pointer + m_backer.writes_offset());
		return *writes;
	}

	Medium::WriteCounter& Medium::writes() noexcept
	{
		WriteCounter* writes = reinterpret_cast<WriteCounter*>(m_pointer + m_backer.writes_offset());
		return *writes;
	}

	const Medium::TicketCounter& Medium::tickets() const noexcept
	{
		TicketCounter* tickets = reinterpret_cast<TicketCounter*>(m_pointer + m_backer.tickets_offset());
		return *tickets;
	}

	Medium::TicketCounter& Medium::tickets() noexcept
	{
		TicketCounter* tickets = reinterpret_cast<TicketCounter*>(m_pointer + m_backer.tickets_offset());
		return *tickets;
	}

	const Medium::SessionMarker& Medium::session() const noexcept
	{
		SessionMarker* session = reinterpret_cast<SessionMarker*>(m_pointer + m_backer.session_offset());
		return *session;
	}

	Medium::SessionMarker& Medium::session() noexcept
	{
		SessionMarker* session = reinterpret_cast<SessionMarker*>(m_pointer + m_backer.session_offset());
		return *session;
	}

	const Buffer Medium::metadata() const noexcept
	{
		char* start = m_pointer + m_backer.metadata_offset();
		size_t size = m_backer.metadata_size();
		return Buffer(start, size);
	}

	const Buffer Medium::component(unsigned int slot, unsigned int component) const
	{
		char* start = m_pointer + m_backer.component_offset(slot, component);
		size_t size = m_backer.component_size(component);
		return Buffer(start, size);
	}

	Buffer Medium::component(unsigned int slot, unsigned int component)
	{
		char* start = m_pointer + m_backer.component_offset(slot, component);
		size_t size = m_backer.component_size(component);
		return Buffer(start, size);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Transport implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	Transport::Transport(Medium& medium) :
		m_medium(medium),
		m_writes(medium.writes()),
		m_tickets(medium.tickets()),
		m_session(medium.session()),
		m_capacity(m_medium.backer().slot_count()),
		m_readable(m_capacity - 1),
		m_writable(1)
	{
		// do nothing
	}

	bool Transport::active() const noexcept
	{
		return m_session.load(std::memory_order_acquire) != INVALID_SESSION_ID;
	}

	Transport::Position Transport::start() const noexcept
	{
		Position writes = m_writes.load(std::memory_order_acquire);
		Position start = (writes < m_readable ? 0 : writes - m_readable);
		return start;
	}

	Transport::Position Transport::middle() const noexcept
	{
		Position writes = m_writes.load(std::memory_order_acquire);
		return writes;
	}

	Transport::Position Transport::until() const noexcept
	{
		Position writes = m_writes.load(std::memory_order_acquire);
		Position until = writes + m_writable - 1;
		return until;
	}

	const Buffer Transport::view(Position position, unsigned int component) const
	{
		Position writes = m_writes.load(std::memory_order_acquire);
		Position start = (writes < m_readable ? 0 : writes - m_readable);

		if (position < start) {
			throw InvalidArgumentException("invalid position", "transport.cpp", __LINE__);
		} else if (position >= writes) {
			throw InvalidArgumentException("invalid position", "transport.cpp", __LINE__);
		} else {
			return m_medium.component(position % m_capacity, component);
		}
	}

	Transport::Session Transport::begin()
	{
		Session session = m_tickets.fetch_add(Session(1), std::memory_order_acquire);
		Session temp = INVALID_SESSION_ID;
		bool result = m_session.compare_exchange_strong(temp, session, std::memory_order_release);

		if (result) {
			return session;
		} else {
			throw ConcurrentSessionException("another session underway", "transport.cpp", __LINE__);
		}
	}

	Buffer Transport::input(Session session, Position position, unsigned int component)
	{
		if (m_session.load(std::memory_order_acquire) == session) {
			Position writes = m_writes.load(std::memory_order_acquire);
			Position until = writes + m_writable - 1;

			if (position > until) {
				throw InvalidArgumentException("invalid position", "transport.cpp", __LINE__);
			} else if (position < writes) {
				throw InvalidArgumentException("invalid position", "transport.cpp", __LINE__);
			} else {
				return m_medium.component(position % m_capacity, component);
			}
		} else {
			throw InvalidArgumentException("invalid session", "transport.cpp", __LINE__);
		}
	}

	void Transport::flush(Session session)
	{
		if (m_session.load(std::memory_order_acquire) == session) {
			m_writes.fetch_add(Position(1), std::memory_order_release);
		} else {
			throw InvalidArgumentException("invalid session", "transport.cpp", __LINE__);
		}
	}

	void Transport::finish(Session session)
	{
		if (m_session.compare_exchange_strong(session, INVALID_SESSION_ID, std::memory_order_release) == false) {
			throw InvalidArgumentException("invalid session", "transport.cpp", __LINE__);
		}
	}

	void Transport::set_readable(unsigned int readable)
	{
		if (readable < 1) {
			throw InvalidArgumentException("invalid readable", "transport.cpp", __LINE__);
		} else if (readable >= m_capacity) {
			throw InvalidArgumentException("invalid readable", "transport.cpp", __LINE__);
		} else {
			m_readable = readable;
			m_writable = std::max(m_writable, m_capacity - m_readable);
		}
	}

	void Transport::set_writable(unsigned int writable)
	{
		if (writable < 1) {
			throw InvalidArgumentException("invalid writable", "transport.cpp", __LINE__);
		} else if (writable >= m_capacity) {
			throw InvalidArgumentException("invalid writable", "transport.cpp", __LINE__);
		} else {
			m_writable = writable;
			m_readable = std::max(m_readable, m_capacity - m_writable);
		}
	}

};


