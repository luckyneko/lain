#include "lain/core/paths.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using lain::core::configDir;

TEST_CASE("configDir returns <home>/.app and creates it", "[core]")
{
	const std::filesystem::path dir = configDir("lain-core-test-xyz");
	REQUIRE_FALSE(dir.empty());						  // HOME is set in any normal test environment
	REQUIRE(dir.filename() == ".lain-core-test-xyz"); // the ~/.app convention
	REQUIRE(std::filesystem::exists(dir));			  // created on demand
	std::filesystem::remove(dir);					  // clean up the empty dir we just made
}
