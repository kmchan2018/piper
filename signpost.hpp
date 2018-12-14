

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
			 * Return the file descriptor that can be polled.
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

			int m_descriptors[2];
			bool m_status;

	};

};


#endif


