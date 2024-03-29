

#include <exception>

#include "exception.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "device.hpp"
#include "tokenbucket.hpp"
#include "operation.hpp"


namespace Piper
{

	//////////////////////////////////////////////////////////////////////////
	//
	// Feed operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	[[ noreturn ]] void FeedOperation::execute(Pipe& pipe, CaptureDevice& device)
	{
		Inlet inlet(pipe);
		Inlet::Position cursor(inlet.start());
		TokenBucket bucket(10, 1, pipe.period_time());

		device.configure(pipe);
		device.start();
		bucket.start();
		m_callback.on_begin_feed(pipe, device);

		try {
			while (true) {
				if (bucket.tokens() == 0) {
					bucket.try_refill();
					m_callback.on_tick();
				} else {
					Preamble& preamble(inlet.preamble(cursor));
					Buffer content(inlet.content(cursor));
					Destination destination(content);

					while (destination.remainder() > 0) {
						device.try_read(destination);
						m_callback.on_tick();
					}

					preamble.timestamp = now();
					m_callback.on_transfer(preamble, content);
					inlet.flush();
					bucket.spend(1);
					cursor++;
				}
			}
		} catch (std::exception& ex) {
			m_callback.on_end();
			bucket.stop();
			device.stop();
			throw;
		} catch (...) {
			m_callback.on_end();
			bucket.stop();
			device.stop();
			throw;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Drain operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	[[ noreturn ]] void DrainOperation::execute(Pipe& pipe, PlaybackDevice& device)
	{
		Outlet outlet(pipe);
		Outlet::Position cursor(outlet.until());
		TokenBucket bucket(10, 1, pipe.period_time());

		try {
			device.configure(pipe, 1);
			device.start();
			bucket.start();
			m_callback.on_begin_drain(pipe, device);

			while (true) {
				if (bucket.tokens() == 0) {
					bucket.try_refill();
					m_callback.on_tick();
				} else if (outlet.until() == cursor) {
					outlet.watch();
					m_callback.on_tick();
				} else if (outlet.start() <= cursor) {
					const Preamble& preamble(outlet.preamble(cursor));
					const Buffer content(outlet.content(cursor));
					Source source(content);

					while (source.remainder() > 0) {
						device.try_write(source);
						m_callback.on_tick();
					}

					m_callback.on_transfer(preamble, content);
					bucket.spend(1);
					cursor += 1;
				} else {
					Support::Exception::start(DrainDataLossException("[Piper::DrainOperation::execute] Cannot continue draining pipe due to cursor underrun"), "operation.cpp", __LINE__);
				}
			}
		} catch (std::exception& ex) {
			m_callback.on_end();
			bucket.stop();
			device.stop();
			throw;
		} catch (...) {
			m_callback.on_end();
			bucket.stop();
			device.stop();
			throw;
		}
	}

}


