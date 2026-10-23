#pragma once

#include <string_view>

namespace lain::app
{
	// The licence notices of everything compiled or linked into this build — what `--licenses`
	// prints. lain's own comes first, then each dependency's: a header line naming it, its version
	// and its licence, then its own licence text, or where that text is staged (ADR-0015, amended
	// 2026-10-23). Every notice is registered whether or not its licence strictly asks for one, so
	// the text is never empty in practice; empty would mean nothing registered at all.
	//
	// The text is GENERATED at configure time from the notices each dependency registered
	// (cmake/lainNotices.cmake), never transcribed, so it cannot claim a dependency this binary
	// does not contain nor keep naming a version it no longer links.
	//
	// Storage is static, so the view outlives any caller.
	[[nodiscard]] std::string_view thirdPartyNotices();
} // namespace lain::app
