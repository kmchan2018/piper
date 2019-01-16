

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <system_error>

#include <errno.h>
#include <unistd.h>

#include "exception.hpp"
#include "signpost.hpp"


#define EXC_START(...) Support::Exception::start(__VA_ARGS__, "signpost.cpp", __LINE__)
#define EXC_CHAIN(...) Support::Exception::chain(__VA_ARGS__, "signpost.cpp", __LINE__);
#define EXC_SYSTEM(err) std::system_error(err, std::system_category(), strerror(err))


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Signpost implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	SignPost::SignPost() : m_descriptors{-1,-1}, m_status(false)
	{
		if (::pipe(m_descriptors) < 0) {
			switch (errno) {
				case EFAULT: EXC_START(std::logic_error("[Piper::SignPost::SignPost] Cannot create signpost due to unexpected error"));
				default: EXC_START(EXC_SYSTEM(errno), SignPostException("[Piper::SignPost::SignPost] Cannot create signpost due to operating system error"));
			}
		}
	}

	SignPost::~SignPost()
	{
		if (m_descriptors[0] >= 0) {
			close(m_descriptors[0]);
			close(m_descriptors[1]);
		}
	}

	void SignPost::activate()
	{
		if (m_status == false) {
			while (true) {
				ssize_t result = ::write(m_descriptors[1], "a", 1);

				if (result > 0) {
					m_status = true;
					return;
				} else if (result < 0 && errno == EBADF) {
					EXC_START(std::logic_error("[Piper::SignPost::activate] Cannot activate signpost due to stale descriptor"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), SignPostException("[Piper::SignPost::activate] Cannot activate signpost due to operating system error"));
				}
			}
		}
	}

	void SignPost::deactivate()
	{
		if (m_status == true) {
			while (true) {
				char buffer[128];
				ssize_t result = ::read(m_descriptors[0], buffer, 1);

				if (result > 0) {
					m_status = false;
					return;
				} else if (result < 0 && errno == EBADF) {
					EXC_START(std::logic_error("[Piper::SignPost::deactivate] Cannot deactivate signpost due to stale descriptor"));
				} else if (result < 0 && errno != EINTR) {
					EXC_START(EXC_SYSTEM(errno), SignPostException("[Piper::SignPost::deactivate] Cannot deactivate signpost due to operating system error"));
				}
			}
		}
	}

}


