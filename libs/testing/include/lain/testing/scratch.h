#pragma once

// Scratch files for tests: a path on disk that no OTHER test can be using.
//
// WHY THIS EXISTS. `catch_discover_tests` registers every TEST_CASE as its own ctest test and runs
// it by invoking the executable again with a filter — so each case is a separate PROCESS. Fourteen
// test files had each grown the same private fixture, uniquifying its path with a
// `static int counter = 0`, under a comment promising "each test owns an isolated fixture on disk".
// That counter restarts at 0 in every process, so under `ctest -j` two cases of one executable took
// the same name and wrote over each other: roughly seven io / io::video failures, nondeterministic,
// and completely invisible to a serial run.
//
// A counter cannot fix that, because the processes cannot see each other's. What can is identity
// minted without coordination, which is exactly what core::Uuid is for — so the uniqueness lives in
// the DIRECTORY, once per process, and a plain counter inside it is then sufficient because Catch2
// runs the cases within one process one at a time.
//
// Header-only and test-only: `lain::testing` is built under LAIN_BUILD_TESTING and linked by test
// targets alone.

#include <lain/core/uuid.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

namespace lain::testing
{
	namespace details
	{
		// Creates the directory on construction and removes it, with everything left in it, on
		// destruction — so a test that forgets to clean up after itself still cannot leak into the
		// next run, and a crashed process leaves exactly one identifiable directory behind.
		//
		// That recursive delete is safe ONLY because the directory is this process's alone, and the
		// point is sharper than it looks: sabotaging the uuid to a fixed name fails MORE tests than
		// the bug this replaces (10-19 against 7), because each finishing process then deletes the
		// directory out from under every other one still running. The uuid buys the cleanup as much
		// as it buys the name.
		class ScratchRoot
		{
		public:
			ScratchRoot()
			{
				// The uuid is what makes this safe across processes. shortString() is display-only
				// and ambiguous by construction, so the FULL id is used: this is a key, not a label.
				m_path = std::filesystem::temp_directory_path() / ("lain-test-" + core::Uuid::generate().toString());
				std::error_code error;
				std::filesystem::create_directories(m_path, error);
			}

			~ScratchRoot()
			{
				std::error_code error;
				std::filesystem::remove_all(m_path, error);
			}

			ScratchRoot(const ScratchRoot&) = delete;
			ScratchRoot& operator=(const ScratchRoot&) = delete;

			const std::filesystem::path& path() const { return m_path; }

		private:
			std::filesystem::path m_path;
		};
	} // namespace details

	// This process's scratch directory. Created on first use and removed at exit, contents and all.
	inline const std::filesystem::path& scratchDir()
	{
		static const details::ScratchRoot root;
		return root.path();
	}

	// A path inside it that nothing else in this process has been given: "<stem>-<n><extension>",
	// where the extension is taken verbatim and so must carry its own dot (or be empty).
	//
	// It is a NAME, not a file — nothing is created here. Half these callers are testing a seam whose
	// whole job is to create the file, and would have to delete it again first.
	inline std::filesystem::path scratchPath(const std::string& stem, const std::string& extension = {})
	{
		static std::size_t counter = 0;
		return scratchDir() / (stem + "-" + std::to_string(counter++) + extension);
	}
} // namespace lain::testing
