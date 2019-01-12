

#include <exception>
#include <stdexcept>


#ifndef SIGNPOST_HPP_
#define SIGNPOST_HPP_


namespace Piper
{

	/**
	 * Signpost implements a file descriptor based, poll(2) friendly signalling
	 * mechanism.
	 *
	 * The mechanism expose a file descriptor that can be polled for incoming
	 * data to the application, plus some methods to control whether incoming
	 * data is available to read on that descriptor.
	 *
	 * Implementation Details
	 * ======================
	 *
	 * The core of the signpost implementation is a socket pair created by the
	 * pipe(2) syscall. The write side of the pair is private to the signpost,
	 * while the read side is available publicly.
	 *
	 * Activation of a signpost is implemented by writing a byte to the write
	 * side of the pair. The action will make the read side readable. On the
	 * other hand, deactivation is done by reading the read side of the pair
	 * until the single byte data is exhausted.
	 *
	 * The implication of this implementation is that the signpost can be
	 * polled for POLLIN event only.
	 *
	 * Weakness
	 * ========
	 *
	 * A weakness of the implementation is that outside parties can read the
	 * read side of the socket pair and cause the internal status to become
	 * out-of-sync with the actual readability of the read side.
	 *
	 */
	class SignPost
	{
		public:

			/**
			 * Construct a new signpost.
			 */
			SignPost();

			/**
			 * Destruct a new signpost and release allocated file descriptors.
			 */
			~SignPost();

			/**
			 * Return the file descriptor for polling. The returned descriptor
			 * can only be polled for POLLIN event, and POLLOUT is not supported.
			 * Also, DO NOT read from the descriptor or it will break the
			 * implementation.
			 */
			int descriptor() const noexcept { return m_descriptors[0]; }

			/**
			 * Return the status of the descriptor. True indicates that the file
			 * descriptor contains incoming data ready to read.
			 */
			bool status() const noexcept { return m_status; }

			/**
			 * Activate the signpost and make the file descriptor ready to read.
			 */
			void activate();

			/**
			 * Deactivate the signpost and make the file descriptor not readable.
			 */
			void deactivate();

			SignPost(const SignPost& signpost) = delete;
			SignPost(SignPost&& signpost) = delete;
			SignPost& operator=(const SignPost& signpost) = delete;
			SignPost& operator=(SignPost&& signpost) = delete;

		private:

			/**
			 * Socket pair backing the signpost. As per the pipe(2) syscall, the
			 * first element is the read side of the socket pair, while the second
			 * element is the write side of the socket pair.
			 */
			int m_descriptors[2];

			/**
			 * Current status of the socket pair read side. True indicates that the
			 * read side of the socket pair has data to read, false otherwise.
			 */
			bool m_status;

	};

	/**
	 * Exception indicating problem with signpost.
	 */
	class SignPostException : public std::runtime_error
	{
		public:
			using std::runtime_error::runtime_error;
	};

};


#endif


