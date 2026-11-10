#pragma once

// Skipping a case in a Debug build, for a case too slow to run there.
//
// WHY THIS EXISTS. Since Catch2 3.9.0, SKIP (like FAIL) ends in a [[noreturn]] call, so in
//
//     #ifndef NDEBUG
//         SKIP("...");
//     #endif
//         <the case's body>
//
// the body is unreachable code in a Debug build, which MSVC reports as C4702 and /WX turns into an
// error. Moving the body under an #else silences that and stops a Debug build compiling the body
// at all, so a change that breaks it is first seen by a Release build; it also strands, in Debug,
// every file-local helper only such a body calls, which GCC's -Wunused-function then refuses. This
// function is deliberately NOT [[noreturn]], so the body stays reachable, and compiled, in every
// build, and the case is still skipped at run time.
//
// Catch2 reports the skip at this header's line rather than the case's; the reason says which case.
//
// Header-only and test-only: `lain::testing` is built under LAIN_BUILD_TESTING and linked by test
// targets alone.

#include <catch2/catch_test_macros.hpp>

namespace lain::testing
{
	// Skips the running case in a Debug build (NDEBUG undefined), and does nothing in any other.
	inline void skipInDebug([[maybe_unused]] const char* reason)
	{
#ifndef NDEBUG
		SKIP(reason);
#endif
	}
} // namespace lain::testing
