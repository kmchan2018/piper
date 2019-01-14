

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>

#include <signal.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "buffer.hpp"
#include "pipe.hpp"
#include "tokenbucket.hpp"
#include "device.hpp"
#include "operation.hpp"
#include "statistics.hpp"
#include "config.h"


using Support::Statistics::make_average;
using Support::Statistics::make_filter;
using Support::Statistics::make_magnitude;
using Support::Statistics::make_divergence;
using Support::Statistics::make_delta;


/**
 * Flag for signalling program reload.
 */
volatile std::atomic_bool reload;


/**
 * Flag for signalling program termination.
 */
volatile std::atomic_bool quit;


/**
 * Exception for reload signal.
 */
class ReloadException : std::exception
{
	public:
		ReloadException(const char* what) : m_what(what) {}
		const char* what() const noexcept { return m_what; }
	private:
		const char* m_what;
};


/**
 * Exception for quit signal.
 */
class QuitException : std::exception
{
	public:
		QuitException(const char* what) : m_what(what) {}
		const char* what() const noexcept { return m_what; }
	private:
		const char* m_what;
};


/**
 * Custom callback implementation for command line interface.
 */
class Callback : public Piper::Callback
{
	public:

		/**
		 * Type code for feed operation.
		 */
		const static int FEED = 1;

		/**
		 * Type code for drain operation.
		 */
		const static int DRAIN = 2;

		/**
		 * Initialize the variables.
		 */
		explicit Callback() :
			m_tracking(false),
			m_operation(0),
			m_write_period_value(make_delta(make_filter(make_average()))),
			m_write_period_jitter(make_delta(make_filter(make_divergence(make_average())))),
			m_transfer_delay_value(make_filter(make_average())),
			m_transfer_delay_jitter(make_filter(make_delta(make_magnitude(make_average()))))
		{
			// do nothing
		}

		/**
		 * Initialize the variables.
		 */
		explicit Callback(bool tracking) :
			m_tracking(tracking),
			m_operation(0),
			m_write_period_value(make_delta(make_filter(make_average()))),
			m_write_period_jitter(make_delta(make_filter(make_divergence(make_average())))),
			m_transfer_delay_value(make_filter(make_average())),
			m_transfer_delay_jitter(make_filter(make_delta(make_magnitude(make_average()))))
		{
			// do nothing
		}

		/**
		 * Initialize tracking for feed operations if tracking is enabled. It
		 * involves setting up the parameters and counters used for statistics
		 * computation.
		 */
		void on_begin_feed(const Piper::Pipe& pipe, const Piper::CaptureDevice& device)
		{
			if (m_tracking) {
				double period = timestamp(pipe.period_time());
				int count = int(1000.0 / period);

				m_operation = FEED;
				m_period = period;
				m_first = true;
				m_write_period_value = make_delta(make_filter(make_average(count), 0.0, 10000.0));
				m_write_period_jitter = make_delta(make_filter(make_divergence(make_average(count), m_period), 0.0, 10000.0));
			}
		}

		/**
		 * Initialize tracking for drain operations if tracking is enabled. It
		 * involves setting up the parameters and counters used for statistics
		 * computation.
		 */
		void on_begin_drain(const Piper::Pipe& pipe, const Piper::PlaybackDevice& device)
		{
			if (m_tracking) {
				double period = timestamp(pipe.period_time());
				int count = int(1000.0 / period);

				m_operation = DRAIN;
				m_period = period;
				m_first = true;
				m_write_period_value = make_delta(make_filter(make_average(count), 0.0, 10000.0));
				m_write_period_jitter = make_delta(make_filter(make_divergence(make_average(count), m_period), 0.0, 10000.0));
				m_transfer_delay_value = make_filter(make_average(count), 0.0, 10000.0);
				m_transfer_delay_jitter = make_filter(make_delta(make_magnitude(make_average(count))), 0.0, 10000.0);
			}
		}

		/**
		 * Handle data transfer during the feed/drain operation by doing nothing.
		 */
		void on_transfer(const Piper::Preamble& preamble, const Piper::Buffer& buffer) override
		{
			if (m_tracking) {
				const double now = timestamp(Piper::now());
				const double current = timestamp(preamble.timestamp);
				const bool first = m_first;

				m_first = false;
				m_write_period_value.consume(current);
				m_write_period_jitter.consume(current);

				if (m_operation == DRAIN) {
					m_transfer_delay_value.consume(now - current);
					m_transfer_delay_jitter.consume(now - current);
				}

				if (m_operation == FEED && first) {
					std::fprintf(stderr, "INFO: Statistics     |       Reference        Measured       Variation\n");
					std::fprintf(stderr, "INFO: ---------------+-------------------------------------------------\n");
					std::fprintf(stderr, "INFO: Write Period   |%16.3f%16.3f%16.3f\n", m_period, m_write_period_value.value(), m_write_period_jitter.value());
				} else if (m_operation == DRAIN && first) {
					std::fprintf(stderr, "INFO: Statistics     |       Reference        Measured       Variation\n");
					std::fprintf(stderr, "INFO: ---------------+-------------------------------------------------\n");
					std::fprintf(stderr, "INFO: Write Period   |%16.3f%16.3f%16.3f\n", m_period, m_write_period_value.value(), m_write_period_jitter.value());
					std::fprintf(stderr, "INFO: Transfer Delay |%16.3f%16.3f%16.3f\n", m_period, m_transfer_delay_value.value(), m_transfer_delay_jitter.value());
				} else if (m_operation == FEED) {
					std::fprintf(stderr, "\x1b[3A\x1b[2K\x1b[1G");
					std::fprintf(stderr, "INFO: Statistics     |       Reference        Measured       Variation\n");
					std::fprintf(stderr, "INFO: ---------------+-------------------------------------------------\n");
					std::fprintf(stderr, "INFO: Write Period   |%16.3f%16.3f%16.3f\n", m_period, m_write_period_value.value(), m_write_period_jitter.value());
				} else if (m_operation == DRAIN) {
					std::fprintf(stderr, "\x1b[4A\x1b[2K\x1b[1G");
					std::fprintf(stderr, "INFO: Statistics     |       Reference        Measured       Variation\n");
					std::fprintf(stderr, "INFO: ---------------+-------------------------------------------------\n");
					std::fprintf(stderr, "INFO: Write Period   |%16.3f%16.3f%16.3f\n", m_period, m_write_period_value.value(), m_write_period_jitter.value());
					std::fprintf(stderr, "INFO: Transfer Delay |%16.3f%16.3f%16.3f\n", m_period, m_transfer_delay_value.value(), m_transfer_delay_jitter.value());
				}
			}
		}

		/**
		 * Handle ticks during the feed/drain operation by checking the reload/quit
		 * flags and throwing appropriate exception.
		 */
		void on_tick() override
		{
			if (quit == true) {
				reload = false;
				quit = false;
				throw QuitException("program termination due to signal");
			} else if (reload == true) {
				reload = false;
				quit = false;
				throw ReloadException("program reload due to signal");
			}
		}

	private:

		/**
		 * Convert timestamps to microsecond floats.
		 */
		double timestamp(Piper::Timestamp timestamp)
		{
			return double(timestamp) / 1000000.0;
		}

		bool m_tracking;
		int m_operation;
		double m_period;
		bool m_first;
		decltype(make_delta(make_filter(make_average()))) m_write_period_value;
		decltype(make_delta(make_filter(make_divergence(make_average())))) m_write_period_jitter;
		decltype(make_filter(make_average())) m_transfer_delay_value;
		decltype(make_filter(make_delta(make_magnitude(make_average())))) m_transfer_delay_jitter;

};


/**
 * Signal handler for setting the reload flag.
 */
extern "C" void trigger_reload(int signum)
{
	reload = true;
}


/**
 * Signal handler for setting the quit flag.
 */
extern "C" void trigger_quit(int signum)
{
	quit = true;
}


/**
 * Print a stack trace of the given exception to standard error stream.
 * The function will walk the exception chain down to the root cause and
 * print them out in that order.
 */
void print_exception(const std::exception& ex, bool initial = true)
{
	auto location = Support::Exception::location(ex);
	auto cause = Support::Exception::cause(ex);
	auto prefix = (initial ? "Exception:" : "Caused by:");

	std::fprintf(stderr, "ERROR: %s %s at file %s line %d\n", prefix, ex.what(), location.file(), location.line());

	if (cause) {
		try {
			std::rethrow_exception(cause);
		} catch (std::exception& ex) {
			print_exception(ex, false);
		} catch (...) {
			// don't care
		}
	}
}


/**
 * Feed pipe from the given device.
 */
template<class Device, class ... Parameters> int do_feed(bool statistics, const char* path, Parameters ... args)
{
	try {
		::signal(SIGTERM, trigger_quit);
		::signal(SIGINT, trigger_quit);
		::signal(SIGQUIT, trigger_quit);
		::signal(SIGHUP, trigger_reload);

		while (true) {
			Callback callback(statistics);
			Piper::FeedOperation operation(callback);
			Piper::Pipe pipe(path);
			Device input(args...);

			try {
				while (true) {
					try {
						operation.execute(pipe, input);
					} catch (Piper::DeviceCaptureException& ex) {
						std::fprintf(stderr, "WARN: Restart feed operation due to capture exception\n");
					}
				}
			} catch (ReloadException& ex) {
				std::fprintf(stderr, "INFO: Reload program due to signal\n");
			}
		}
	} catch (QuitException& ex) {
		return 0;
	} catch (Piper::EndOfFileException& ex) {
		return 0;
	} catch (std::exception& ex) {
		std::fprintf(stderr, "ERROR: Cannot feed pipe due to exception\n");
		print_exception(ex);
		return 3;
	} catch (...) {
		std::fprintf(stderr, "ERROR: Cannot feed pipe\n\n");
		return 3;
	}

	return 0;
}


/**
 * Drain pipe to the given device.
 */
template<class Device, class ... Parameters> int do_drain(bool statistics, const char* path, Parameters ... args)
{
	try {
		::signal(SIGTERM, trigger_quit);
		::signal(SIGINT, trigger_quit);
		::signal(SIGQUIT, trigger_quit);
		::signal(SIGHUP, trigger_reload);

		while (true) {
			Callback callback(statistics);
			Piper::DrainOperation operation(callback);
			Piper::Pipe pipe(path);
			Device output(args...);

			try {
				while (true) {
					try {
						operation.execute(pipe, output);
					} catch (Piper::DrainDataLossException& ex) {
						std::fprintf(stderr, "WARN: Restart drain operation due to pipe buffer overrun\n");
					} catch (Piper::DevicePlaybackException& ex) {
						std::fprintf(stderr, "WARN: Restart drain operation due to playback exception\n");
					}
				}
			} catch (ReloadException& ex) {
				std::fprintf(stderr, "INFO: Reload program due to signal\n");
			}
		}
	} catch (QuitException& ex) {
		return 0;
	} catch (std::exception& ex) {
		std::fprintf(stderr, "ERROR: Cannot drain pipe due to exception\n");
		print_exception(ex);
		return 3;
	} catch (...) {
		std::fprintf(stderr, "ERROR: Cannot drain pipe\n\n");
		return 3;
	}

	return 0;
}


/**
 * Create a new pipe.
 */
int create(int argc, char **argv)
{
	if (argc >= 10) {
		const char* format = argv[3];
		Piper::Channel channels = std::atoi(argv[4]);
		Piper::Rate rate = std::atoi(argv[5]);
		Piper::Duration period = std::atoi(argv[6]) * 1000000;
		unsigned int readable = std::atoi(argv[7]);
		unsigned int writable = std::atoi(argv[8]);
		unsigned int separation = std::atoi(argv[9]);

		if (snd_pcm_format_value(format) == SND_PCM_FORMAT_UNKNOWN) {
			std::fprintf(stderr, "ERROR: Format %s is not recognized\n\n", format);
			return 2;
		} else if (channels == 0) {
			std::fprintf(stderr, "ERROR: Channels cannot be zero\n\n");
			return 2;
		} else if (rate == 0) {
			std::fprintf(stderr, "ERROR: Rate cannot be zero\n\n");
			return 2;
		} else if (readable <= 1) {
			std::fprintf(stderr, "ERROR: Readable should be larger than 1\n\n");
			return 2;
		} else if (writable <= 1) {
			std::fprintf(stderr, "ERROR: Writable should be larger than 1\n\n");
			return 2;
		}

		try {
			Piper::Pipe pipe(argv[2], format, channels, rate, period, readable, writable, separation, 0640);
			return 0;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: Cannot create pipe due to exception\n");
			print_exception(ex);
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s create <path> <format> <channels> <rate> <period> <buffer> <capacity>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Print information about a pipe.
 */
int info(int argc, char **argv)
{
	if (argc >= 3) {
		try {
			Piper::Pipe pipe(argv[2]);
			const Piper::Transport& transport(pipe.transport());
			const Piper::Medium& medium(transport.medium());
			const Piper::Backer& backer(medium.backer());

			std::fprintf(stderr, "\n");
			std::fprintf(stderr, "  Pipe details\n");
			std::fprintf(stderr, " ------------------------------------------------------\n");
			std::fprintf(stderr, "  Format: %s\n", pipe.format_name());
			std::fprintf(stderr, "  Channels: %u\n", pipe.channels());
			std::fprintf(stderr, "  Sampling Rate: %u\n", pipe.rate());
			std::fprintf(stderr, "  Frame: %zu bytes\n", pipe.frame_size());
			std::fprintf(stderr, "  Period: %zu bytes or %lu ns\n", pipe.period_size(), pipe.period_time());
			std::fprintf(stderr, "  Readable: %u periods or %zu bytes or %lu ns\n", pipe.readable(), pipe.readable_size(), pipe.readable_time());
			std::fprintf(stderr, "  Writable: %u periods or %zu bytes or %lu ns\n", pipe.writable(), pipe.writable_size(), pipe.writable_time());
			std::fprintf(stderr, "  Capacity: %u periods or %zu bytes or %lu ns\n", pipe.capacity(), pipe.capacity_size(), pipe.capacity_time());
			std::fprintf(stderr, "\n");
			std::fprintf(stderr, "  Transport details\n");
			std::fprintf(stderr, " ------------------------------------------------------\n");
			std::fprintf(stderr, "  Slot Count: %u\n", backer.slot_count());
			std::fprintf(stderr, "  Component Count: %u\n", backer.component_count());
			std::fprintf(stderr, "  Metadata Size: %zu\n", backer.metadata_size());
			std::fprintf(stderr, "  Component Sizes: ");

			for (unsigned int i = 0; i < backer.component_count(); i++) {
				if (i == 0) {
					std::fprintf(stderr, "%zu", backer.component_size(i));
				} else {
					std::fprintf(stderr, ", %zu", backer.component_size(i));
				}
			}

			std::fprintf(stderr, " bytes\n");
			std::fprintf(stderr, "\n");
			std::fprintf(stderr, "  Layout details\n");
			std::fprintf(stderr, " ------------------------------------------------------\n");
			std::fprintf(stderr, "  Header Offset: %zu\n", backer.header_offset());
			std::fprintf(stderr, "  Metadata Offset: %zu\n", backer.metadata_offset());
			std::fprintf(stderr, "  Component Offsets: ");

			for (unsigned int i = 0; i < backer.component_count(); i++) {
				if (i == 0) {
					std::fprintf(stderr, "%zu", backer.component_offset(0, i));
				} else {
					std::fprintf(stderr, ", %zu", backer.component_offset(0, i));
				}
			}

			std::fprintf(stderr, "\n");
			std::fprintf(stderr, "  Total Size: %zu\n", backer.total_size());
			std::fprintf(stderr, "\n");

			return 0;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: Cannot dump pipe due to exception\n");
			print_exception(ex);
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s info <path>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Feed pipe from capture device.
 */
int feed(int argc, char **argv)
{
	char* path = nullptr;
	char* device = nullptr;
	bool statistics = false;

	for (int i = 2; i < argc; i++) {
		if (std::strcmp(argv[i], "-s") == 0) {
			statistics = true;
		} else if (path == nullptr) {
			path = argv[i];
		} else if (device == nullptr) {
			device = argv[i];
		}
	}

	if (path == nullptr) {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s feed [-s] <path> [<device>]\n\n", argv[0]);
		return 1;
	}

	if (device == nullptr) {
		return do_feed<Piper::StdinCaptureDevice>(statistics, path);
	} else if (strcmp(device, "-") == 0) {
		return do_feed<Piper::StdinCaptureDevice>(statistics, path);
	} else if (strcmp(device, "stdin") == 0) {
		return do_feed<Piper::StdinCaptureDevice>(statistics, path);
	} else if (strcmp(device, "alsa") == 0) {
		return do_feed<Piper::AlsaCaptureDevice>(statistics, path, "default");
	} else if (strncmp(device, "alsa:", 5) == 0) {
		return do_feed<Piper::AlsaCaptureDevice>(statistics, path, device + 5);
	} else {
		std::fprintf(stderr, "ERROR: Unknown capture device %s\n", device);
		return 1;
	}
}


/**
 * Drain pipe to stdout.
 */
int drain(int argc, char **argv)
{
	char* path = nullptr;
	char* device = nullptr;
	bool statistics = false;

	for (int i = 2; i < argc; i++) {
		if (std::strcmp(argv[i], "-s") == 0) {
			statistics = true;
		} else if (path == nullptr) {
			path = argv[i];
		} else if (device == nullptr) {
			device = argv[i];
		}
	}

	if (path == nullptr) {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s drain [-s] <path> [<device>]\n\n", argv[0]);
		return 1;
	}

	if (device == nullptr) {
		return do_drain<Piper::StdoutPlaybackDevice>(statistics, path);
	} else if (strcmp(device, "-") == 0) {
		return do_drain<Piper::StdoutPlaybackDevice>(statistics, path);
	} else if (strcmp(device, "stdin") == 0) {
		return do_drain<Piper::StdoutPlaybackDevice>(statistics, path);
	} else if (strcmp(device, "alsa") == 0) {
		return do_drain<Piper::AlsaPlaybackDevice>(statistics, path, "default");
	} else if (strncmp(device, "alsa:", 5) == 0) {
		return do_drain<Piper::AlsaPlaybackDevice>(statistics, path, device + 5);
	} else {
		std::fprintf(stderr, "ERROR: Unknown playback device %s\n", device);
		return 1;
	}
}


/**
 * Unclog a pipe.
 */
int unclog(int argc, char **argv)
{
	if (argc >= 3) {
		try {
			Piper::Backer backer(argv[2]);
			Piper::Medium medium(backer);
			Piper::Medium::SessionMarker& session = medium.session();
			session = 0;

			return 0;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: Cannot unclog pipe due to exception\n");
			print_exception(ex);
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s info <path>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Print program version.
 */
int version(int argc, char** argv)
{
	std::fprintf(stderr, "Piper version %d.%d.%d\n", PIPER_VERSION_MAJOR, PIPER_VERSION_MINOR, PIPER_VERSION_PATCH);
	std::fprintf(stderr, "Usage: %s create|info|feed|drain|unclog|version <parameter>...\n\n", argv[0]);
	return 0;
}


/**
 * Main program.
 */
int main(int argc, char **argv)
{
	if (argc >= 2 && std::strcmp(argv[1], "create") == 0) {
		return create(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "info") == 0) {
		return info(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "feed") == 0) {
		return feed(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "drain") == 0) {
		return drain(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "unclog") == 0) {
		return unclog(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "version") == 0) {
		return version(argc, argv);
	} else if (argc >= 2) {
		std::fprintf(stderr, "ERROR: Unknown subcommand %s\n", argv[1]);
		std::fprintf(stderr, "Usage: %s create|info|feed|drain|unclog|version <parameter>...\n\n", argv[0]);
		return 1;
	} else {
		std::fprintf(stderr, "Usage: %s create|info|feed|drain|unclog|version <parameter>...\n\n", argv[0]);
		return 0;
	}
}


