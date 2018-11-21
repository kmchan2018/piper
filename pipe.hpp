

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "mmap.hpp"


#ifndef PIPE_HPP_
#define PIPE_HPP_


namespace Piper
{

	/**
	 * Pipe header contains common properties of the pipe and is stored
	 * in the initial page of the backer. It contains the following
	 * fields:
	 *
	 * `stride` defines the size of each block in the buffer. It is the
	 * basic unit the pipe can read or write at one time.
	 *
	 * `capacity` indicates the size of the buffer in term of blocks. It
	 * includes both the flushed blocks and a single staging block.
	 *
	 * `writes` is an atomic counter tracking the number of blocks written to
	 * the stream. It also indicates the position of the staging block in the
	 * buffer. Please note that each stream can only have one write position
	 * shared among all users.
	 *
	 * `tickets` is an atomic counter tracking the number of tickets issued.
	 * The counter is used to derive a globally unique identifier for update
	 * sessions.
	 *
	 * `session` is an atomic variable tracking the current update session.
	 * Only the session starter is allowed to update the pipe. If there is no
	 * active session, the sentinel value INVALID_SESSION_ID is stored.
	 */
	struct Header
	{
		std::uint32_t stride;
		std::uint32_t capacity;
		std::uint32_t period;
		std::atomic_uint64_t writes;
		std::atomic_uint64_t tickets;
		std::atomic_uint64_t session;
	};

	/**
	 * Pipe backer represents the mappable file where the pipe is stored.
	 *
	 * The file consists of two areas. The first area stores the header
	 * of the pipe and is a single page long. The second area stores the
	 * data in the pipe and its length is determined by the stride and
	 * capacity of the pipe.
	 */
	class Backer
	{
		public:

			/**
			 * Create a new backer file with the given parameters. Stride specifies
			 * the size of each block, capacity specifies the number of blocks kept
			 * by the pipe, and period advises the time between each flush. The
			 * method will throw exception when it cannot create the backer file or
			 * mmap the shared memory.
			 */
			explicit Backer(const char* path, std::uint32_t stride, std::uint32_t capacity, std::uint32_t period);

			/**
			 * Open an existing backer file. The method will throw exception when it
			 * cannot open the backer file or mmap the shared memory. Additionally,
			 * the method can fail if the file is not fully initialized yet.
			 */
			explicit Backer(const char* path);

			/**
			 * Get the path of the backer file.
			 */
			const std::string& path() const noexcept { return m_path; }

			/**
			 * Get the stride of the backer file.
			 */
			std::uint32_t stride() const noexcept { return m_stride; }

			/**
			 * Get the capacity of the backer file.
			 */
			std::uint32_t capacity() const noexcept { return m_capacity; }

			/**
			 * Get the period of the backer file.
			 */
			std::uint32_t period() const noexcept { return m_period; }

			/**
			 * Get the file instance of the backer file.
			 */
			const File& file() const noexcept { return m_file; }

			/**
			 * Get the offset to the header section of the backer file.
			 */
			std::size_t header_offset() const noexcept { return 0; }

			/**
			 * Get the length of the header section of the backer file.
			 */
			std::size_t header_length() const noexcept { return m_pagesize; }

			/**
			 * Get the offset to the buffer section of the backer file.
			 */
			std::size_t buffer_offset() const noexcept { return m_pagesize; }

			/**
			 * Get the length of the buffer section of the backer file.
			 */
			std::size_t buffer_length() const noexcept { return m_stride * m_capacity; }

			Backer(const Backer& backer) = delete;
			Backer(Backer&& backer) = delete;
			Backer& operator=(const Backer& backer) = delete;
			Backer& operator=(Backer&& backer) = delete;

		private:
			int m_pagesize;
			std::string m_path;
			std::uint32_t m_stride;
			std::uint32_t m_capacity;
			std::uint32_t m_period;
			File m_file;
	};

	/**
	 * Pipe implements a data stream where blocks can be transferred between
	 * different processes via shared memory.
	 *
	 * Conceptual Model
	 * ================
	 *
	 * Conceptually a pipe is a list of fixed-size blocks.
	 *
	 * In the pipe, there are two types of blocks. At the end of the queue there
	 * is always a single staging block where incoming data is cached before the
	 * actual write. The block is restricted to the update session holder only.
	 * Other blocks in the queue are flushed blocks and contains finalized data
	 * for others to consume. To actually write the data in the staging block,
	 * a flush operation is executed which turns a staging block into a flushed
	 * block, as well as appending a new staging block to the end of the queue.
	 *
	 * Each block in the pipe has an unique index based on its position in the
	 * list, ie the order the blocks are written. The first block ever written
	 * into the pipe will become block 0, the second block block 1, and so on.
	 *
	 * However, to minimize memory use, the pipe will only keep a limited amount
	 * of recent blocks and discards the rest. When the pipe always keep the
	 * maximum number of blocks, appending a new block will cause the pipe to
	 * remove the oldest block in a FIFO manner.
	 *
	 */
	class Pipe
	{
		public:

			/**
			 * Open an existing pipe. The method will throw exception when it 
			 * cannot open the pipe file or mmap the shared memory. Additionally,
			 * the method can fail if the pipe is not fully initialized yet.
			 */
			explicit Pipe(Backer& backer);

			/**
			 * Get the backer of the pipe.
			 */
			Backer& backer() const noexcept { return m_backer; }

			/**
			 * Get the stride of the pipe.
			 */
			std::uint32_t stride() const noexcept { return m_header->stride; }

			/**
			 * Get the capacity of the pipe.
			 */
			std::uint32_t capacity() const noexcept { return m_header->capacity; }

			/**
			 * Get the period of the pipe.
			 */
			std::uint32_t period() const noexcept { return m_header->period; }

			/**
			 * Get the index of the first block available in the pipe. Blocks that
			 * are earlier than this block is discarded by the pipe.
			 */
			std::uint64_t start() const noexcept;

			/**
			 * Get the index of the last block available in the pipe. This block
			 * should be a staging block.
			 */
			std::uint64_t until() const noexcept;

			/**
			 * Get whether the pipe is active or not; here active means there are
			 * an active writer.
			 */
			bool active() const noexcept;

			/**
			 * Get the content of the specified flushed block. The method will
			 * throws an invalid argument exception in three conditions: the block
			 * may be discarded by the pipe, or the block is the current staging
			 * block, or the block does not exist yet.
			 */
			const Buffer view(std::uint64_t block) const;

			/**
			 * Start an update session and returns the session id. The method will
			 * throws a concurrent session exception when another update session
			 * is under way.
			 */
			std::uint64_t begin();

			/**
			 * Get the current staging block so that the writer can prepare its
			 * content. The method requires an active update session id, and throws
			 * an invalid argument exception when an incorrect session id is provided.
			 */
	    Buffer staging(std::uint64_t sid) const;

			/**
			 * Flush the current staging block. The current staging block will be
			 * converted to a flushed block, and a new staging block will be appended
			 * to the end of the pipe. The method requires an active update session id,
			 * and throws an invalid argument exception when an incorrect session id 
			 * is provided.
			 */
	    void flush(std::uint64_t sid);

			/**
			 * Finish the active update session. The method requires an active update
			 * session id, and throws an invalid argument exception when an incorrect
			 * session id is provided.
			 */
	    void finish(std::uint64_t sid);

			Pipe(const Pipe& inlet) = delete;
			Pipe(Pipe&& inlet) = delete;
			Pipe& operator=(const Pipe& inlet) = delete;
			Pipe& operator=(Pipe&& inlet) = delete;

		private:
			Backer& m_backer;
			MappedStruct<Header> m_header;
			MappedBuffer m_buffer;
	};

	/**
	 * Inlet implements a pipe writer that feeds data to the given pipe. It manages
	 * the lifecycle of pipe update session lifecycle in a RAII manner and provides
	 * wrapper methods for `staging` and `flush` as well.
	 */
	class Inlet
	{
		public:

			/**
			 * Construct a new inlet for the given pipe. It will create an active
			 * update session that will persist until the inlet is destructed. The
			 * constructor will throws a concurrent session exception when another
			 * update session is under way.
			 */
			explicit Inlet(Pipe& pipe);

			/**
			 * Destruct the inlet. The update session will be finished.
			 */
			~Inlet();

			/**
			 * Return the pipe the inlet is attached to.
			 */
			const Pipe& pipe() const noexcept { return m_pipe; }

			/**
			 * Return the update session of the inlet.
			 */
			std::uint64_t session() const noexcept { return m_session; }

			/**
			 * Get the current staging block so that the writer can prepare its
			 * content.
			 */
	    Buffer staging() const;

			/**
			 * Flush the current staging block. The current staging block will be
			 * converted to a flushed block, and a new staging block will be appended
			 * to the end of the pipe.
			 */
	    void flush();

			Inlet(const Inlet& inlet) = delete;
			Inlet(Inlet&& inlet) = delete;
			Inlet& operator=(const Inlet& inlet) = delete;
			Inlet& operator=(Inlet&& inlet) = delete;

		private:
			Pipe& m_pipe;
			std::uint64_t m_session;
	};

	/**
	 * Outlet implements a pipe reader that drains data from the given pipe.
	 *
	 * Each outlet will maintain its own read position which indicates the block
	 * being processed. The read position may refer to any blocks in the pipe.
	 * Normally, it will point to a flushed block between `start` and `until`.
	 * However, there are 2 exceptional possibilities. If it refers to the
	 * current staging block, the whole pipe is already drained at this time and
	 * no more block is available. If it refers to any flushed blocks before
	 * `start`, some of the unconsumed blocks are already overwritten by the
	 * pipe and is lost.
	 */
	class Outlet
	{
		public:

			/**
			 * Construct a new inlet for the given pipe. The outlet will attempt to
			 * set the current block to the last flushed block; if that fails, the
			 * the current block will be the staging block.
			 */
			explicit Outlet(const Pipe& pipe);

			/**
			 * Return the pipe the outlet is attached to.
			 */
			const Pipe& pipe() const noexcept { return m_pipe; }

			/**
			 * Return if the current block is a flushed block available in the pipe.
			 */
			bool valid() const noexcept;

			/**
			 * Return if the number of blocks that are discarded by the pipe before
			 * the outlet handles them.
			 */
			std::uint32_t loss() const noexcept;

			/**
			 * Return if the number of blocks that are available for processing by
			 * the outlet.
			 */
			std::uint32_t available() const noexcept;

			/**
			 * Return the buffer containing the content of current flushed block.
			 */
			const Buffer view() const;

			/**
			 * Wait until some blocks are available for consumption. The method will
			 * only return when the outlet contains data to read.
			 */
			void wait();

			/**
			 * Attempt to wait until some blocks are available for consumption. The
			 * method will return only when:
			 *
			 * 1. The outlet contains data to read; or
			 * 2. The process receives POSIX signal.
			 *
			 * This call is equivalent to calling the `try_wait(int)` method with
			 * timeout -1.
			 */
			void try_wait();

			/**
			 * Attempt to wait until some blocks are available for consumption. The
			 * method will return only when:
			 *
			 * 1. The outlet contains data to read; or
			 * 2. The process receives POSIX signal; or
			 * 3. The specified timeout has elapsed.
			 *
			 * Note that the timeout accepts 2 special values. The timeout of 0 means
			 * no waiting which makes no sense but accepted nontheless. The timeout of
			 * -1 indicates that timeout is not observed.
			 */
			void try_wait(int timeout);

			/**
			 * Move to the next block.
			 */
			void drain();

			/**
			 * Recover the outlet so that the current block points to an available
			 * block.
			 */
			void recover(uint32_t target);

			Outlet(const Outlet& inlet) = delete;
			Outlet(Outlet&& inlet) = delete;
			Outlet& operator=(const Outlet& inlet) = delete;
			Outlet& operator=(Outlet&& inlet) = delete;

		private:
			const Pipe& m_pipe;
			std::uint64_t m_reads;
			volatile bool m_cancel;
	};

	/**
	 * Exception indicating that the client wants to start a new update session
	 * while another one is underway.
	 */
	class ConcurrentSessionException : public Exception
	{
		public:
			using Exception::Exception;
	};

};


#endif


