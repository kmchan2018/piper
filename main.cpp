

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE


#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <signal.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "exception.hpp"
#include "timestamp.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"
#include "tokenbucket.hpp"


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
 * Flag for signalling program termination.
 */
volatile std::atomic_bool quit;

/**
 * Handle signals by setting the quit flag.
 */
void stop(int signum) {
	std::fprintf(stderr, "INFO: Start program termination due to signal\n\n");
	quit = true;
}

/**
 * Check the quit flag and raise quit exception if true.
 */
void check() {
	if (quit == true) {
		throw QuitException("program termination due to signal");
	}
}


/**
 * Create a new pipe.
 */
int create(int argc, char **argv) {
	if (argc >= 9) {
		Piper::Format format = snd_pcm_format_value(argv[3]);
		Piper::Channel channels = std::atoi(argv[4]);
		Piper::Rate rate = std::atoi(argv[5]);
		Piper::Duration period = std::atoi(argv[6]) * 1000000;
		unsigned int buffer = std::atoi(argv[7]);
		unsigned int capacity = std::atoi(argv[8]);

		if (format == SND_PCM_FORMAT_UNKNOWN) {
			std::fprintf(stderr, "ERROR: format %s not recognized\n\n", argv[3]);
			return 2;
		} else if (channels == 0) {
			std::fprintf(stderr, "ERROR: channels cannot be zero\n\n");
			return 2;
		} else if (rate == 0) {
			std::fprintf(stderr, "ERROR: rate cannot be zero\n\n");
			return 2;
		} else if (buffer <= 1) {
			std::fprintf(stderr, "ERROR: buffer should be larger than 1\n\n");
			return 2;
		} else if (capacity <= buffer) {
			std::fprintf(stderr, "ERROR: capacity should be larger than buffer\n\n");
			return 2;
		}

		try {
			Piper::Pipe pipe(argv[2], format, channels, rate, period, buffer, capacity, 0640);
			return 0;
		} catch (Piper::Exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s at file %s line %d\n\n", ex.what(), ex.file(), ex.line());
			return 3;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s\n\n", ex.what());
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
			fprintf(stderr, "  Format: %s\n", snd_pcm_format_name(pipe.format()));
			fprintf(stderr, "  Channels: %u\n", pipe.channels());
			fprintf(stderr, "  Sampling Rate: %u\n", pipe.rate());
			fprintf(stderr, "  Frame: %zu bytes\n", pipe.frame_size());
			fprintf(stderr, "  Period: %zu bytes or %lu ns\n", pipe.period_size(), pipe.period_time());
			fprintf(stderr, "  Writable: %u periods or %zu bytes or %lu ns\n", pipe.buffer(), pipe.buffer_size(), pipe.buffer_time());
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
		} catch (Piper::Exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s at file %s line %d\n\n", ex.what(), ex.file(), ex.line());
			return 3;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s\n\n", ex.what());
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s info <path>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Feed pipe from stdin.
 */
int feed(int argc, char **argv) {
	if (argc >= 3) {
		try {
			Piper::File input(STDIN_FILENO);
			Piper::Pipe pipe(argv[2]);
			Piper::Inlet inlet(pipe);
			Piper::Inlet::Position cursor(inlet.start());
			Piper::TokenBucket bucket(10, 1, pipe.period_time());

			quit = false;
			signal(SIGTERM, stop);
			signal(SIGINT, stop);
			signal(SIGQUIT, stop);
			signal(SIGHUP, stop);

			bucket.start();

			while (quit == false) {
				try {
					if (bucket.tokens() == 0) {
						while (bucket.tokens() == 0) {
							bucket.try_refill();
							check();
						}
					} else {
						Piper::Preamble& preamble(inlet.preamble(cursor));
						Piper::Buffer content(inlet.content(cursor));
						Piper::Destination destination(content);

						while (destination.remainder() > 0) {
							input.try_readall(destination);
							check();
						}

						preamble.timestamp = Piper::now();
						inlet.flush();
						bucket.spend(1);
						cursor++;
					}
				} catch (Piper::EOFException& ex) {
					break;
				} catch (QuitException& ex) {
					break;
				}
			}

			return 0;
		} catch (Piper::Exception& ex) {
			std::fprintf(stderr, "ERROR: cannot feed pipe: %s at file %s line %d\n\n", ex.what(), ex.file(), ex.line());
			return 3;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot feed pipe: %s\n\n", ex.what());
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s feed <path>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Drain pipe to stdout.
 */
int drain(int argc, char **argv) {
	if (argc >= 3) {
		try {
			Piper::File output(STDOUT_FILENO);
			Piper::Pipe pipe(argv[2]);
			Piper::Outlet outlet(pipe);
			Piper::Outlet::Position cursor(outlet.until());
			Piper::TokenBucket bucket(10, 1, pipe.period_time());

			quit = false;
			signal(SIGTERM, stop);
			signal(SIGINT, stop);
			signal(SIGQUIT, stop);
			signal(SIGHUP, stop);

			bucket.start();

			while (quit == false) {
				try {
					if (bucket.tokens() == 0) {
						while (bucket.tokens() == 0) {
							bucket.try_refill();
							check();
						}
					} else if (outlet.until() == cursor) {
						while (outlet.until() == cursor) {
							outlet.watch();
							check();
						}
					} else if (outlet.start() > cursor) {
						std::fprintf(stderr, "WARNING: discarding old data\n");
						cursor = outlet.until();
					} else {
						const Piper::Buffer content(outlet.content(cursor));
						Piper::Source source(content);

						while (source.remainder() > 0) {
							output.try_writeall(source);
							check();
						}

						bucket.spend(1);
						cursor++;
					}
				} catch (Piper::EOFException& ex) {
					break;
				} catch (QuitException& ex) {
					break;
				}
			}

			return 0;
		} catch (Piper::Exception& ex) {
			std::fprintf(stderr, "ERROR: cannot drain pipe: %s at file %s line %d\n\n", ex.what(), ex.file(), ex.line());
			return 3;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot drain pipe: %s\n\n", ex.what());
			return 3;
		}
	} else {
		std::fprintf(stderr, "ERROR: Missing arguments\n");
		std::fprintf(stderr, "Usage: %s drain <path>\n\n", argv[0]);
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
		} catch (Piper::Exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s at file %s line %d\n\n", ex.what(), ex.file(), ex.line());
			return 3;
		} catch (std::exception& ex) {
			std::fprintf(stderr, "ERROR: cannot create pipe: %s\n\n", ex.what());
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
int main(int argc, char **argv) {
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


