#pragma once

#include <string_view>

namespace lain::app
{
	// The third-party notice text this binary is obliged to make available — what `--licenses`
	// prints. Empty when nothing linked into the build carries such an obligation, which is the
	// normal case for lain's permissive dependencies (ADR-0015).
	//
	// The text is GENERATED at configure time from the notices each dependency registered
	// (cmake/lainNotices.cmake), never transcribed, so it cannot claim a dependency this binary
	// does not contain nor keep naming a version it no longer links. FFmpeg's LGPL is the first
	// and so far only contributor (ADR-0019).
	//
	// Storage is static, so the view outlives any caller.
	[[nodiscard]] std::string_view thirdPartyNotices();
} // namespace lain::app
