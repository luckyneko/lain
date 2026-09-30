// Every real-camera fixture committed under plugins/camera/test/fixtures must pass (ADR-0016).
//
// A fixture is a folder holding fixture.json. Its compact tier is what is committed, and it runs
// wherever a camera backend does. Its extended tier, when the folder also holds extended.json, is a
// larger capture kept outside the repository and found under $LAIN_CAMERA_FIXTURE_DATA: verified by
// content hash and then run, reported unavailable where the data is not present, and a failure
// where data is present that the manifest does not pin.
//
// With no fixture committed yet this SKIPs and says so, rather than passing as though one had run.

#include "fixture/fixture.h"
#include "fixture/manifest.h"
#include "registration.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/calibration/estimator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

using namespace lain;
using namespace lain::camera::fixture;
namespace fs = std::filesystem;

namespace
{
	std::vector<fs::path> committedFixtures()
	{
		std::vector<fs::path> found;
		std::error_code ec;
		const fs::path root = LAIN_CAMERA_FIXTURES_DIR;
		for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
			if (it->is_directory(ec) && fs::is_regular_file(it->path() / kFixtureFile, ec))
				found.push_back(it->path());
		std::sort(found.begin(), found.end());
		return found;
	}

	void run(const fs::path& root, const char* tier)
	{
		INFO(tier << " tier of '" << root.generic_string() << "'");
		const FixtureLoad load = loadFixture(root);
		for (const std::string& problem : load.problems)
			UNSCOPED_INFO(problem);
		REQUIRE(load.fixture.has_value());
		const FixtureReport report = runFixture(*load.fixture);
		// Always shown, passing or not: the numbers are what a person reads a fixture for.
		WARN(report.toString());
		CHECK(report.passed());
	}
} // namespace

TEST_CASE("every committed real-camera fixture passes", "[camera][fixture][real]")
{
	const std::vector<fs::path> fixtures = committedFixtures();
	if (fixtures.empty())
		SKIP("no real-camera capture is committed under plugins/camera/test/fixtures yet");
	ensureRegistered();
	if (!camera::board::canDetect() || !camera::calibration::canEstimate())
		SKIP("this build has no camera backend");

	for (const fs::path& root : fixtures)
	{
		run(root, "compact");

		const fs::path manifestFile = root / kExtendedManifest;
		std::error_code ec;
		if (!fs::is_regular_file(manifestFile, ec))
			continue;
		const ManifestRead read = readManifest(manifestFile);
		INFO("'" << manifestFile.generic_string() << "'");
		for (const std::string& problem : read.problems)
			UNSCOPED_INFO(problem);
		REQUIRE(read.manifest.has_value());
		const ExtendedTier tier = verifyExtended(*read.manifest, dataRootFromEnvironment());
		switch (tier.status)
		{
			case TierStatus::Verified:
				run(tier.root, "extended");
				break;
			case TierStatus::Unavailable:
				WARN("extended tier unavailable: " << tier.reason);
				break;
			case TierStatus::Mismatch:
				FAIL("the extended data is not the data its manifest pins: " << tier.reason);
				break;
		}
	}
}
