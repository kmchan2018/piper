

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

	void FeedOperation::execute(Pipe& pipe, CaptureDevice& device)
	{
		Inlet inlet(pipe);
		Inlet::Position cursor(inlet.start());
		TokenBucket bucket(10, 1, pipe.period_time());

		device.configure(pipe);
		device.start();
		bucket.start();

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
		} catch (Exception& ex) {
			bucket.stop();
			device.stop();
			throw;
		} catch (std::exception& ex) {
			bucket.stop();
			device.stop();
			throw;
		} catch (...) {
			bucket.stop();
			device.stop();
			throw;
		}

		bucket.stop();
		device.stop();
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Drain operation implementation.
	//
	//////////////////////////////////////////////////////////////////////////

	void DrainOperation::execute(Pipe& pipe, PlaybackDevice& device)
	{
		Outlet outlet(pipe);
		Outlet::Position cursor(outlet.until());
		TokenBucket bucket(10, 1, pipe.period_time());

		try {
			device.configure(pipe, 1);
			device.start();
			bucket.start();

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
					throw DrainDataLossException("cursor behind outlet start", "operation.cpp", __LINE__);
				}
			}
		} catch (Exception& ex) {
			bucket.stop();
			device.stop();
			throw;
		} catch (std::exception& ex) {
			bucket.stop();
			device.stop();
			throw;
		} catch (...) {
			bucket.stop();
			device.stop();
			throw;
		}

		bucket.stop();
		device.stop();
	}

};


