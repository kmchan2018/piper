

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
class QuitException : std::exception {
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
		 * Initialize the variables.
		 */
		explicit Callback() : m_tracking(false) {}

		/**
		 * Disable tracking for drain operations.
		 */
		void on_begin_feed(const Piper::Pipe& pipe, const Piper::CaptureDevice& device)
		{
			m_tracking = false;
		}

		/**
		 * Initialize tracking for drain operations.
		 */
		void on_begin_drain(const Piper::Pipe& pipe, const Piper::PlaybackDevice& device)
		{
			const double period = timestamp(pipe.period_time());

			m_tracking = true;
			m_alpha = 2.0 / (1000.0 / period + 1.0);
			m_remainder = 1.0 - m_alpha;
			m_previous = std::nan("1");
			m_source_delay_expectation = period;
			m_source_delay_average = std::nan("1");
			m_source_delay_max = std::nan("1");
			m_source_jitter_average = std::nan("1");
			m_source_jitter_max = std::nan("1");
			m_pipe_delay_expectation = period / 2.0;
			m_pipe_delay_average = std::nan("1");
			m_pipe_delay_max = std::nan("1");
			m_pipe_jitter_average = std::nan("1");
			m_pipe_jitter_max = std::nan("1");
		}

		/**
		 * Handle data transfer during the feed/drain operation by doing nothing.
		 */
		void on_transfer(const Piper::Preamble& preamble, const Piper::Buffer& buffer) override
		{
			if (m_tracking) {
				const double now = timestamp(Piper::now());
				const double current = timestamp(preamble.timestamp);
				const double previous = m_previous;

				m_previous = current;

				const double pipe_delay = now - current;
				const double pipe_jitter = abs(pipe_delay - m_pipe_delay_expectation);

				if (pipe_delay < 10000.0) {
					if (std::isnan(m_pipe_delay_average) == false) {
						m_pipe_delay_average = m_alpha * pipe_delay + m_remainder * m_pipe_delay_average;
						m_pipe_delay_max = std::max(pipe_delay, m_pipe_delay_max);
						m_pipe_jitter_average = m_alpha * pipe_jitter + m_remainder * m_pipe_jitter_average;
						m_pipe_jitter_max = std::max(pipe_jitter, m_pipe_jitter_max);
					} else {
						m_pipe_delay_average = pipe_delay;
						m_pipe_delay_max = pipe_delay;
						m_pipe_jitter_average = pipe_jitter;
						m_pipe_jitter_max = pipe_jitter;
					}
				}

				if (std::isnan(previous) == false) {
					const double source_delay = current - previous;
					const double source_jitter = abs(source_delay - m_source_delay_expectation);

					if (source_delay < 10000.0) {
						if (std::isnan(m_source_delay_average) == false) {
							m_source_delay_average = m_alpha * source_delay + m_remainder * m_source_delay_average;
							m_source_delay_max = std::max(source_delay, m_source_delay_max);
							m_source_jitter_average = m_alpha * source_jitter + m_remainder * m_source_jitter_average;
							m_source_jitter_max = std::max(source_jitter, m_source_jitter_max);
						} else {
							m_source_delay_average = source_delay;
							m_source_delay_max = source_delay;
							m_source_jitter_average = source_jitter;
							m_source_jitter_max = source_jitter;
						}
					}
				}

				std::fprintf(stderr, "\x1b[2K\x1b[1GDEBUG: source: delay=(%5.3f, %5.3f), jitter=(%5.3f, %5.3f), pipe: delay=(%5.3f, %5.3f), jitter=(%5.3f, %5.3f)",
					m_source_delay_average, m_source_delay_max,
					m_source_jitter_average, m_source_jitter_max,
					m_pipe_delay_average, m_pipe_delay_max,
					m_pipe_jitter_average, m_pipe_jitter_max);
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

		/**
		 * Handle the end of operation by printing a new line to standard output
		 * when tracking is active.
		 */
		void on_end() override
		{
			if (m_tracking) {
				std::fprintf(stderr, "\n");
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
		double m_alpha;
		double m_remainder;
		double m_previous;
		double m_source_delay_expectation;
		double m_source_delay_average;
		double m_source_delay_max;
		double m_source_jitter_average;
		double m_source_jitter_max;
		double m_pipe_delay_expectation;
		double m_pipe_delay_average;
		double m_pipe_delay_max;
		double m_pipe_jitter_average;
		double m_pipe_jitter_max;

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
	auto prefix = (initial ? "Exception:" : "> Caused by:");

	std::fprintf(stderr, "%s %s at file %s line %d\n", prefix, ex.what(), location.file(), location.line());

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
template<class Device, class ... Parameters> int do_feed(const char* path, Parameters ... args)
{
	try {
		signal(SIGTERM, trigger_quit);
		signal(SIGINT, trigger_quit);
		signal(SIGQUIT, trigger_quit);
		signal(SIGHUP, trigger_reload);

		while (true) {
			Callback callback;
			Piper::FeedOperation operation(callback);
			Piper::Pipe pipe(path);
			Device input(args...);

			try {
				while (true) {
					try {
						operation.execute(pipe, input);
					} catch (Piper::DeviceCaptureException& ex) {
						std::fprintf(stderr, "WARN: feed restarted due to capture exception\n");
						print_exception(ex);
					}
				}
			} catch (ReloadException& ex) {
				std::fprintf(stderr, "INFO: program reloaded due to signal\n");
			}
		}
	} catch (QuitException& ex) {
		return 0;
	} catch (Piper::EndOfFileException& ex) {
		return 0;
	} catch (std::exception& ex) {
		std::fprintf(stderr, "ERROR: cannot feed pipe due to exception\n");
		print_exception(ex);
		return 3;
	} catch (...) {
		std::fprintf(stderr, "ERROR: cannot feed pipe\n\n");
		return 3;
	}

	return 0;
}


/**
 * Drain pipe to the given device.
 */
template<class Device, class ... Parameters> int do_drain(const char* path, Parameters ... args)
{
	try {
		signal(SIGTERM, trigger_quit);
		signal(SIGINT, trigger_quit);
		signal(SIGQUIT, trigger_quit);
		signal(SIGHUP, trigger_reload);

		while (true) {
			Callback callback;
			Piper::DrainOperation operation(callback);
			Piper::Pipe pipe(path);
			Device output(args...);

			try {
				while (true) {
					try {
						operation.execute(pipe, output);
					} catch (Piper::DrainDataLossException& ex) {
						std::fprintf(stderr, "WARN: drain restarted due to pipe buffer overrun\n");
						print_exception(ex);
					} catch (Piper::DevicePlaybackException& ex) {
						std::fprintf(stderr, "WARN: drain restarted due to playback exception\n");
						print_exception(ex);
					}
				}
			} catch (ReloadException& ex) {
				std::fprintf(stderr, "INFO: program reloaded due to signal\n");
			}
		}
	} catch (QuitException& ex) {
		return 0;
	} catch (std::exception& ex) {
		std::fprintf(stderr, "ERROR: cannot drain pipe due to exception\n");
		print_exception(ex);
		return 3;
	} catch (...) {
		std::fprintf(stderr, "ERROR: cannot drain pipe\n\n");
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
			std::fprintf(stderr, "ERROR: format %s is not recognized\n\n", format);
			return 2;
		} else if (channels == 0) {
			std::fprintf(stderr, "ERROR: channels cannot be zero\n\n");
			return 2;
		} else if (rate == 0) {
			std::fprintf(stderr, "ERROR: rate cannot be zero\n\n");
			return 2;
		} else if (readable <= 1) {
			std::fprintf(stderr, "ERROR: readable should be larger than 1\n\n");
			return 2;
		} else if (writable <= 1) {
			std::fprintf(stderr, "ERROR: writable should be larger than 1\n\n");
			return 2;
		}

		try {
			Piper::Pipe pipe(argv[2], format, channels, rate, period, readable, writable, separation, 0640);
			return 0;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe due to exception\n");
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

			fprintf(stderr, "\n");
			fprintf(stderr, "  Pipe details\n");
			fprintf(stderr, " ======================================================\n");
			fprintf(stderr, "  Format: %s\n", pipe.format_name());
			fprintf(stderr, "  Channels: %u\n", pipe.channels());
			fprintf(stderr, "  Sampling Rate: %u\n", pipe.rate());
			fprintf(stderr, "  Frame: %zu bytes\n", pipe.frame_size());
			fprintf(stderr, "  Period: %zu bytes or %lu ns\n", pipe.period_size(), pipe.period_time());
			fprintf(stderr, "  Readable: %u periods or %zu bytes or %lu ns\n", pipe.readable(), pipe.readable_size(), pipe.readable_time());
			fprintf(stderr, "  Writable: %u periods or %zu bytes or %lu ns\n", pipe.writable(), pipe.writable_size(), pipe.writable_time());
			fprintf(stderr, "  Capacity: %u periods or %zu bytes or %lu ns\n", pipe.capacity(), pipe.capacity_size(), pipe.capacity_time());
			fprintf(stderr, "\n");
			fprintf(stderr, "  Transport details\n");
			fprintf(stderr, " ======================================================\n");
			fprintf(stderr, "  Slot Count: %u\n", backer.slot_count());
			fprintf(stderr, "  Component Count: %u\n", backer.component_count());
			fprintf(stderr, "  Metadata Size: %zu\n", backer.metadata_size());
			fprintf(stderr, "  Component Sizes: ");

			for (unsigned int i = 0; i < backer.component_count(); i++) {
				if (i == 0) {
					fprintf(stderr, "%zu", backer.component_size(i));
				} else {
					fprintf(stderr, ", %zu", backer.component_size(i));
				}
			}

			fprintf(stderr, " bytes\n");
			fprintf(stderr, "\n");
			fprintf(stderr, "  Layout details\n");
			fprintf(stderr, " ======================================================\n");
			fprintf(stderr, "  Header Offset: %zu\n", backer.header_offset());
			fprintf(stderr, "  Metadata Offset: %zu\n", backer.metadata_offset());
			fprintf(stderr, "  Component Offsets: ");

			for (unsigned int i = 0; i < backer.component_count(); i++) {
				if (i == 0) {
					fprintf(stderr, "%zu", backer.component_offset(0, i));
				} else {
					fprintf(stderr, ", %zu", backer.component_offset(0, i));
				}
			}

			fprintf(stderr, "\n");
			fprintf(stderr, "  Total Size: %zu\n", backer.total_size());
			fprintf(stderr, "\n");

			return 0;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot dump pipe due to exception\n");
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
	if (argc < 3) {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s feed <path> [<device>]\n\n", argv[0]);
		return 1;
	}

	if (argc == 3) {
		return do_feed<Piper::StdinCaptureDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "-") == 0) {
		return do_feed<Piper::StdinCaptureDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "stdin") == 0) {
		return do_feed<Piper::StdinCaptureDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "alsa") == 0) {
		return do_feed<Piper::AlsaCaptureDevice>(argv[2], "default");
	} else if (argc == 4 && strncmp(argv[3], "alsa:", 5) == 0) {
		return do_feed<Piper::AlsaCaptureDevice>(argv[2], argv[3] + 5);
	} else {
		std::fprintf(stderr, "ERROR: unknown capture device %s\n", argv[3]);
		return 1;
	}
}


/**
 * Drain pipe to stdout.
 */
int drain(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s drain <path> [<device>]\n\n", argv[0]);
		return 1;
	}

	if (argc == 3) {
		return do_drain<Piper::StdoutPlaybackDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "-") == 0) {
		return do_drain<Piper::StdoutPlaybackDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "stdin") == 0) {
		return do_drain<Piper::StdoutPlaybackDevice>(argv[2]);
	} else if (argc == 4 && strcmp(argv[3], "alsa") == 0) {
		return do_drain<Piper::AlsaPlaybackDevice>(argv[2], "default");
	} else if (argc == 4 && strncmp(argv[3], "alsa:", 5) == 0) {
		return do_drain<Piper::AlsaPlaybackDevice>(argv[2], argv[3] + 5);
	} else {
		std::fprintf(stderr, "ERROR: unknown capture device %s\n", argv[3]);
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
			std::fprintf(stderr, "ERROR: cannot unclog pipe due to exception\n");
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
	} else if (argc >= 2) {
		std::fprintf(stderr, "ERROR: Unknown subcommand %s\n", argv[1]);
		std::fprintf(stderr, "Usage: %s create|feed|drain <parameter>...\n\n", argv[0]);
		return 1;
	} else {
		std::fprintf(stderr, "Usage: %s create|feed|drain <parameter>...\n\n", argv[0]);
		return 0;
	}
}


