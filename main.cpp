

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

#include "exception.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "pipe.hpp"
#include "tokenbucket.hpp"
#include "preamble.hpp"


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
	if (argc >= 6) {
		unsigned int stride = std::atoi(argv[3]);
		unsigned int capacity = std::atoi(argv[4]);
		unsigned int period = std::atoi(argv[5]);

		if (stride == 0) {
			std::fprintf(stderr, "ERROR: argument stride cannot be zero\n\n");
			return 2;
		} else if (capacity < 2) {
			std::fprintf(stderr, "ERROR: argument capacity cannot be zero\n\n");
			return 2;
		} else if (period == 0) {
			std::fprintf(stderr, "ERROR: argument period cannot be zero\n\n");
			return 2;
		}

		try {
			Piper::Backer backer(argv[2], stride + sizeof(Piper::Preamble), capacity, period);
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
		std::fprintf(stderr, "Usage: %s create <path> <stride> <capacity> <period>\n\n", argv[0]);
		return 1;
	}
}


/**
 * Feed pipe from stdin.
 */
int feed(int argc, char **argv) {
	if (argc >= 2) {
		try {
			Piper::Backer backer(argv[2]);
			Piper::Pipe pipe(backer);
			Piper::Inlet inlet(pipe);
			Piper::File input(STDIN_FILENO);
			Piper::TokenBucket bucket(10, 1, pipe.period());

			const size_t head_size = sizeof(Piper::Preamble);
			const size_t body_size = pipe.stride() - head_size;

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
						Piper::Buffer staging(inlet.staging());
						Piper::Buffer head(staging.head(head_size));
						Piper::Buffer body(staging.tail(body_size));
						Piper::Destination destination(body);

						while (destination.remainder() > 0) {
							input.try_readall(destination);
							check();
						}

						copy(head, Piper::Preamble());
						inlet.flush();
						bucket.spend(1);
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
	if (argc >= 2) {
		try {
			Piper::Backer backer(argv[2]);
			Piper::Pipe pipe(backer);
			Piper::Outlet outlet(pipe);
			Piper::File output(STDOUT_FILENO);
			Piper::TokenBucket bucket(10, 1, pipe.period());

			const size_t head_size = sizeof(Piper::Preamble);
			const size_t body_size = pipe.stride() - head_size;

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
					} else if (outlet.valid() == false && outlet.available() == 0) {
						while (outlet.available() == 0) {
							outlet.try_wait();
							check();
						}
					} else if (outlet.valid() == false && outlet.loss() > 0) {
						std::fprintf(stderr, "WARN: discarding old data\n");
						outlet.recover(1);
					} else {
						Piper::Buffer view(outlet.view());
						Piper::Buffer head(view.head(head_size));
						Piper::Buffer body(view.tail(body_size));
						Piper::Preamble preamble(head);
						Piper::Source source(body);

						while (source.remainder() > 0) {
							output.try_writeall(source);
							check();
						}

						outlet.drain();
						bucket.spend(1);
						std::fprintf(stderr, "INFO: latency = %0.2f ms\n", preamble.latency_ms());
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
 * Main program.
 */
int main(int argc, char **argv) {
	if (argc >= 2 && std::strcmp(argv[1], "create") == 0) {
		return create(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "feed") == 0) {
		return feed(argc, argv);
	} else if (argc >= 2 && std::strcmp(argv[1], "drain") == 0) {
		return drain(argc, argv);
	} else if (argc >= 2) {
		std::fprintf(stderr, "ERROR: Unknown subcommand %s\n", argv[1]);
		std::fprintf(stderr, "Usage: %s create|feed|drain <parameter>...\n\n", argv[0]);
		return 1;
	} else {
		std::fprintf(stderr, "Usage: %s create|feed|drain <parameter>...\n\n", argv[0]);
		return 0;
	}
}


