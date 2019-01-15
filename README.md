
# Introduction

This is a simple program and ALSA plugin that sends audio data around with
minimum overhead. I use it to send audio from/to QEMU virtual machines.

# Building

The program can be built by cmake. For example:

    cmake .
    make
    make install

# How to Use

To use the program, a pipe file have to be created first. It can be done
via the `create` subcommand. The subcommand requires several parameters:

1. Format of the audio sample (eg: `S16_LE`)
1. Number of channels per frame
1. Sampling Frequency in Hertz (eg: `44100` for CD quality audio)
1. Period Duration in microseconds
1. Number of periods available for write buffering
1. Number of periods available for read buffering
1. Number of periods separating the read/write buffers

For example, if we want to pass around stereo CD quality audio, the pipe
can be created with:

    piper create pipe-file S16_LE 2 44100 10 500 500 100

Access to the pipe is controlled by ownership and permission of the pipe
file.

To write audio data into the pipe, the `feed` subcommand can be used. It
reads samples from standard input or selected ALSA device and write them
into the pipe.

    piper feed pipe-file # read from stdin
    piper feed pipe-file stdin # same as above
    piper feed pipe-file alsa # read from default ALSA device
    piper feed pipe-file alsa:device # read from named ALSA device

To read audio data from the pipe, the `drain` subcommand can be used. It
reads samples from the pipe and writes them to standard output or selected
ALSA device.

		piper drain pipe-file # write to stdout
		piper drain pipe-file stdout # same as above
		piper drain pipe-file alsa # write to default ALSA device
		piper drain pipe-file alsa:device # write to named ALSA device

Other subcommands include `info` which prints out details about a pipe file,
`unclog` subcommand which recovers a clogged pipe file due to unexpected 
program termination and `version` subcommand which prints out the program
version.

To use the ALSA plugin, the plugin may need to be registered first
depending on how the program is installed. To do so, include the
following configuration snippet in your ALSA configuration file:

    pcm_type.piper {
      lib path-to-alsa-plugin-solib.so
    }

After that, a new PCM device have to be created like this:

    pcm.piper {
      type piper
      playback pipe-file-used-in-playback-mode
      capture pipe-file-used-in-capture-mode
    }

# Issues

If ALSA is compiled with thread-safety and the PCM device is layered behind
another PCM plugin, the program may deadlock. It can be solved by setting
the `LIBASOUND_THREAD_SAFE` environment variable.

# License

The software is licensed under MIT license. Refer to LICENSE for more
details.

