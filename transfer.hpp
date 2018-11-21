

#include "exception.hpp"
#include "buffer.hpp"


#ifndef TRANSFER_HPP_
#define TRANSFER_HPP_


namespace Piper
{

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
					throw InvalidArgumentException("invalid consumed", "transfer.hpp", __LINE__);
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
					throw InvalidArgumentException("invalid consumed", "transfer.hpp", __LINE__);
				}
			}

		private:
			Buffer& m_buffer;
			unsigned int m_remainder;
	};

};


#endif


