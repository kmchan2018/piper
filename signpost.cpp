

#include <cstdio>
#include <cstring>

#include <errno.h>
#include <unistd.h>

#include "exception.hpp"
#include "signpost.hpp"


namespace Piper
{

	SignPost::SignPost() : m_descriptors{-1,-1}, m_status(false)
	{
		if (::pipe(m_descriptors) < 0) {
			throw SystemException("cannot create signpost", "signpost.cpp", __LINE__);
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
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot activate the signpost", "signpost.cpp", __LINE__);
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
				} else if (result < 0 && errno != EINTR) {
					throw SystemException("cannot deactivate the signpost", "signpost.cpp", __LINE__);
				}
			}
		}
	}

};


