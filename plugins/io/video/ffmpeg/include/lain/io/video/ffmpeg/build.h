#pragma once

#include <string>

namespace lain::io::video::ffmpeg
{
	// What the LINKED FFmpeg reports about itself, asked through its own API rather than
	// inferred from the build recipe that produced it.
	//
	// This exists because the licence lives in the configuration, not in the API called: an
	// FFmpeg built --enable-gpl relicenses whatever links it whether or not a GPL codec is
	// ever invoked (ADR-0019). cmake/addFFmpeg.cmake gates on the prebuilt archive's
	// MANIFEST.txt, which needs no execution and so survives cross-compiling; these two
	// functions are the runtime second opinion, and the only gate at all for an FFmpeg
	// someone supplied via LAIN_FFMPEG_ROOT. Two independent things must be wrong at once
	// for a GPL-configured library to reach a build.
	//
	// They are the plugin's whole public surface until the reader lands beside them.

	// The full configure string (avutil_configuration), e.g. "--prefix=... --enable-shared
	// --disable-static ...". Never empty for a working FFmpeg.
	std::string configuration();

	// The licence name (avutil_license), e.g. "LGPL version 2.1 or later".
	std::string license();
} // namespace lain::io::video::ffmpeg
