

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <exception>
#include <vector>
#include <stdexcept>

#include "buffer.hpp"
#include "file.hpp"


#ifndef TRANSPORT_HPP_
#define TRANSPORT_HPP_


namespace Piper
{

	/**
	 * Backer represents the backing file where a transport is stored. The backing
	 * file contains at least three areas.
	 *
	 * The first area stores the header of the transport that contains essential
	 * information about the transport. It includes sizes of different parts as
	 * well as operational parameter of the transport.
	 *
	 * The second area stores the metadata of the transport. Metadata includes
	 * application specific data related to the transport. It is specified when
	 * the transport is created and should be updated after that.
	 *
	 * After that, each component in the block will have its own area. The area
	 * will contain component data from every block tightly packed without any
	 * padding.
	 *
	 * Each area are padded to make sure that the next block will start at the
	 * page boundary. It ensure that header, metadata and each block component
	 * are properly aligned.
	 */
	class Backer
	{
		public:

			typedef std::uint64_t Position;
			typedef std::uint64_t Session;
			typedef std::atomic<Position> WriteCounter;
			typedef std::atomic<Session> TicketCounter;
			typedef std::atomic<Session> SessionMarker;

			/**
			 * Create a new backer file with the given parameters. The method will
			 * throw exception when it cannot create the file.
			 */
			explicit Backer(const char* path, const Buffer& metadata, const std::vector<std::size_t> components, unsigned int slots, unsigned int mode);

			/**
			 * Open an existing backer file. The method will throw exception when it
			 * cannot open the backing file. Additionally, the method can fail if the
			 * file is not fully initialized yet.
			 */
			explicit Backer(const char* path);

			/**
			 * Return the path of the backing file.
			 */
			const std::string& path() const noexcept { return m_path; }

			/**
			 * Return the backing file.
			 */
			const File& file() const noexcept { return m_file; }

			/**
			 * Return the backing file.
			 */
			File& file() noexcept { return m_file; }

			/**
			 * Return the offset to the header area.
			 */
			std::size_t header_offset() const noexcept { return 0; }

			/**
			 * Return the size of the header area.
			 */
			std::size_t header_size() const noexcept { return sizeof(Header); }

			/**
			 * Return the offset to the write counter.
			 */
			std::size_t writes_offset() const noexcept { return offsetof(Header, writes); }

			/**
			 * Return the offset to the ticket counter.
			 */
			std::size_t tickets_offset() const noexcept { return offsetof(Header, tickets); }

			/**
			 * Return the offset to the session marker.
			 */
			std::size_t session_offset() const noexcept { return offsetof(Header, session); }

			/**
			 * Return the offset to the metadata area.
			 */
			std::size_t metadata_offset() const noexcept { return m_metadata_offset; }

			/**
			 * Return the size of the metadata area.
			 */
			std::size_t metadata_size() const noexcept { return m_metadata_size; }

			/**
			 * Return the number of slots.
			 */
			unsigned int slot_count() const noexcept { return m_slot_count; }

			/**
			 * Return the number of the components in each block.
			 */
			unsigned int component_count() const noexcept { return m_component_count; }

			/**
			 * Return the offset to the specific component area.
			 */
			std::size_t component_offset(unsigned int slot, unsigned int component) const;

			/**
			 * Return the size of the specific component area.
			 */
			std::size_t component_size(unsigned int component) const;

			/**
			 * Return the total size.
			 */
			std::size_t total_size() const noexcept { return m_total_size; }

		private:

			/**
			 * Transport version.
			 */
			static const std::uint32_t VERSION = 1;

			/**
			 * Component limit represents the maximum number of components a
			 * block can have in the transport.
			 */
			static const unsigned int MAX_COMPONENT_COUNT = 15;

			/**
			 * Channel header contains common properties of the transport and is
			 * stored in the initial page of the mapped file. It contains the
			 * following fields:
			 *
			 * `slot_count` indicates the size of the transport in term of blocks.
			 * It includes both the readbale and writable blocks.
			 *
			 * `component_count` indicates the number of components each block
			 * contains.
			 *
			 * The size fields define the size of various transport elements in
			 * bytes: `page_size` for memory page; `metadata_size` for transport
			 * metadata; `component_sizes` for each block component.
			 *
			 * `writes` is an atomic counter tracking the number of blocks written
			 * to the transport. It also indicates the position of the first writable
			 * block in the transport. Please note that each transport can only have
			 * one write position shared among all users.
			 *
			 * `tickets` is an atomic counter tracking the number of tickets issued.
			 * The counter is used to derive a globally unique identifier for update
			 * sessions.
			 *
			 * `session` is an atomic variable tracking the current update session.
			 * Only the session starter is allowed to update the transport. If there
			 * is no active session, a special sentinel value is stored.
			 */
			struct Header
			{
				std::uint32_t version = VERSION;
				std::uint32_t slot_count;
				std::uint32_t component_count;
				std::uint32_t page_size;
				std::uint32_t metadata_size;
				std::uint32_t component_sizes[MAX_COMPONENT_COUNT];
				WriteCounter writes;
				TicketCounter tickets;
				SessionMarker session;
			};

			static_assert(sizeof(WriteCounter) == sizeof(Position), "incorrect layout for transport header");
			static_assert(sizeof(TicketCounter) == sizeof(Session), "incorrect layout for transport header");
			static_assert(sizeof(SessionMarker) == sizeof(Session), "incorrect layout for transport header");

			static_assert(offsetof(Header, slot_count) - offsetof(Header, version) == sizeof(Header::version), "incorrect layout for transport header");
			static_assert(offsetof(Header, component_count) - offsetof(Header, slot_count) == sizeof(Header::slot_count), "incorrect layout for transport header");
			static_assert(offsetof(Header, page_size) - offsetof(Header, component_count) == sizeof(Header::component_count), "incorrect layout for transport header");
			static_assert(offsetof(Header, metadata_size) - offsetof(Header, page_size) == sizeof(Header::page_size), "incorrect layout for transport header");
			static_assert(offsetof(Header, component_sizes) - offsetof(Header, metadata_size) == sizeof(Header::metadata_size), "incorrect layout for transport header");
			static_assert(offsetof(Header, writes) - offsetof(Header, component_sizes) == sizeof(Header::component_sizes), "incorrect layout for transport header");
			static_assert(offsetof(Header, tickets) - offsetof(Header, writes) == sizeof(Header::tickets), "incorrect layout for transport header");
			static_assert(offsetof(Header, session) - offsetof(Header, tickets) == sizeof(Header::session), "incorrect layout for transport header");
			static_assert(sizeof(Header) - offsetof(Header, session) == sizeof(Header::session), "incorrect layout for transport header");

			static_assert(sizeof(unsigned int) >= sizeof(std::uint32_t), "possible precision loss");
			static_assert(sizeof(std::size_t) >= sizeof(std::uint32_t), "possible precision loss");

			std::string m_path;
			File m_file;
			unsigned int m_slot_count;
			unsigned int m_component_count;
			std::size_t m_page_size;
			std::size_t m_header_offset;
			std::size_t m_header_size;
			std::size_t m_metadata_offset;
			std::size_t m_metadata_size;
			std::size_t m_component_offsets[MAX_COMPONENT_COUNT];
			std::size_t m_component_sizes[MAX_COMPONENT_COUNT];
			std::size_t m_total_size;

	};

	/**
	 * Medium represents shared memory created from the backing file
	 * for interprocess communication.
	 */
	class Medium
	{
		public:

			typedef Backer::Position Position;
			typedef Backer::Session Session;
			typedef Backer::WriteCounter WriteCounter;
			typedef Backer::TicketCounter TicketCounter;
			typedef Backer::SessionMarker SessionMarker;

			/**
			 * Create a medium from the backing file.
			 */
			explicit Medium(Backer& backer);

			/**
			 * Release the shared memory.
			 */
			~Medium();

			/**
			 * Return the backing file.
			 */
			const Backer& backer() const noexcept { return m_backer; }

			/**
			 * Return the reference to the write counter.
			 */
			const WriteCounter& writes() const noexcept;

			/**
			 * Return the reference to the write counter.
			 */
			WriteCounter& writes() noexcept;

			/**
			 * Return the reference to the ticket counter.
			 */
			const TicketCounter& tickets() const noexcept;

			/**
			 * Return the reference to the ticket counter.
			 */
			TicketCounter& tickets() noexcept;

			/**
			 * Return the reference to the session marker.
			 */
			const SessionMarker& session() const noexcept;

			/**
			 * Return the reference to the session marker.
			 */
			SessionMarker& session() noexcept;

			/**
			 * Return the buffer to the metadata region.
			 */
			const Buffer metadata() const noexcept;

			/**
			 * Return the buffer to the a block component.
			 */
			const Buffer component(unsigned int slot, unsigned int component) const;

			/**
			 * Return the buffer to the a block component.
			 */
			Buffer component(unsigned int slot, unsigned int component);

		private:

			Backer& m_backer;
			std::size_t m_size;
			char* m_pointer;

	};

	/**
	 * Transport implements a communication protocol where blocks can be passed
	 * between different processes via shared memory.
	 *
	 * Concept
	 * =======
	 *
	 * Conceptually a transport is a sequence of fixed-size blocks.
	 *
	 * Each block in the transport has an unique index based on its position in
	 * the the sequence. The first block in the sequence will be block 0, the
	 * second block block 1, and so on.
	 *
	 * Each block can contain multiple fixed-size parts called components. They
	 * are identified by an 0-based index. Each component should store either
	 * binary data or a struct where struct size matches the component size.
	 *
	 * In the transport there are two types of blocks. At the end of the sequence
	 * there is always some writable blocks where new data is stored before the
	 * the final commit. The blocks are restricted to the update session holder
	 * only. Other blocks in the queue are readable blocks and contains data
	 * for others to consume.
	 *
	 * Considerations
	 * ==============
	 *
	 * Logically a transport has no limits on its length. However, as the
	 * transport is used for sending time sensitive data, it is obvious that we
	 * only have to keep only recent blocks in the transport and discard the
	 * rest.
	 *
	 * Another consideration is that usually data will be sent over the transport
	 * periodically in a fixed period.
	 *
	 * Implementation
	 * ==============
	 *
	 * A fixed capacity ring buffer is used to implement the transport. The
	 * details should be obvious, except a few things of note:
	 *
	 * 1. Each block has a fixed location in the ring buffer determined by its
	 *    index.
	 *
	 * 2. There are no reader or other entity to remove blocks from the ring.
	 *    Old blocks are simply discarded when space is needed for new block.
	 *
	 * 3. The implementation uses a counter to track the number of written
	 *    blocks. The value implies the first writable block, its location in
	 *    the ring buffer as well as the index and location of the preceding
	 *    readable blocks.
	 *
	 * 4. As the transport is used for interprocess communication, writes to the
	 *    transport should be coordinated. It is done by update session. Only
	 *    one client can start an update session, and only client with update
	 *    session can write to the transport.
	 */
	class Transport
	{
		public:

			typedef Medium::Position Position;
			typedef Medium::Session Session;

			/**
			 * Create a transport from the medium.
			 */
			explicit Transport(Medium& medium);

			/**
			 * Get the medium.
			 */
			const Medium& medium() const noexcept { return m_medium; }

			/**
			 * Get the read window aka the maximum number of readable blocks.
			 */
			unsigned int readable() const noexcept { return m_readable; }

			/**
			 * Get the write window aka the maximum number of writable blocks.
			 */
			unsigned int writable() const noexcept { return m_writable; }

			/**
			 * Get the metadata of the transport.
			 */
			const Buffer metadata() const noexcept { return m_medium.metadata(); }

			/**
			 * Get whether the transport is active or not; here active means there
			 * are an active writer.
			 */
			bool active() const noexcept;

			/**
			 * Get the index of the first block available in the transport. Blocks
			 * that are earlier than this block is discarded and lost forever.
			 */
			Position start() const noexcept;

			/**
			 * Get the index of the middle block available in the transport. This
			 * block should be the first writable block.
			 */
			Position middle() const noexcept;

			/**
			 * Get the index of the last block available in the transport. This
			 * block should be the last writable block.
			 */
			Position until() const noexcept;

			/**
			 * Get an immutable view to the given component of the given readable
			 * block. The method will throw an invalid argument exception when the
			 * block is either discarded, writable, or simply non-existent.
			 */
			const Buffer view(Position position, unsigned int component) const;

			/**
			 * Start an update session and returns the session id. The method will
			 * throws a concurrent session exception when another update session
			 * is under way.
			 */
			Session begin();

			/**
			 * Get an input buffer to the given component of the given writable
			 * block. The method requires an active update session id, and throws
			 * an invalid argument exception when an incorrect session id is
			 * provided.
			 */
	    Buffer input(Session session, Position position, unsigned int component);

			/**
			 * Flush the first writable block. The block will be converted into a
			 * readable block, and a new writable block will be appended to the end
			 * of the transport. The method requires an active update session id and
			 * throws an invalid argument exception when an incorrect session id is
			 * provided.
			 */
	    void flush(Session sid);

			/**
			 * Finish the active update session. The method requires an active update
			 * session id, and throws an invalid argument exception when an incorrect
			 * session id is provided.
			 */
	    void finish(Session sid);

			/**
			 * Adjust the read window. The write window will be adjusted when
			 * necessary.
			 */
			void set_readable(unsigned int readable);

			/**
			 * Adjust the write window. The read window will be adjusted when
			 * necessary.
			 */
			void set_writable(unsigned int writable);

			Transport(const Transport& transport) = delete;
			Transport(Transport&& transport) = delete;
			Transport& operator=(const Transport& transport) = delete;
			Transport& operator=(Transport&& transport) = delete;

		private:

			Medium& m_medium;
			Medium::WriteCounter& m_writes;
			Medium::TicketCounter& m_tickets;
			Medium::SessionMarker& m_session;
			unsigned int m_capacity;
			unsigned int m_readable;
			unsigned int m_writable;

	};

	/**
	 * Exception thrown for any transport errors.
	 */
	class TransportException : public std::runtime_error
	{
		public:
			using std::runtime_error::runtime_error;
	};

	/**
	 * Exception thrown for input/output operation failures on transport and
	 * its backer file.
	 */
	class TransportIOException : public TransportException
	{
		public:
			using TransportException::TransportException;
	};

	/**
	 * Exception thrown for corrupted backer files.
	 */
	class TransportCorruptedException : public TransportException
	{
		public:
			using TransportException::TransportException;
	};

	/**
	 * Exception indicating that the client wants to start a new update session
	 * in a transport while another one is underway.
	 */
	class TransportConcurrentSessionException : public TransportException
	{
		public:
			using TransportException::TransportException;
	};

};


#endif


