// Targetless registration's evidence through the Ceres refiner: held-out tracks predicted member by
// member with every camera held (registration::validate), one refinement per track. The stand-in
// scene is libs/camera/test's (syntheticscene.h). Every tolerance is a measurement, written beside its
// check.

#include "syntheticscene.h"

#include <lain/camera/ceres/register.h>
#include <lain/camera/registration/validation.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
namespace scene = lain::camera::testing::scene;

namespace
{
	void ensureCeres()
	{
		static const bool once = []
		{
			lain::camera::ceres::registerBackend();
			return true;
		}();
		(void)once;
	}

	std::vector<RigCamera> rig()
	{
		std::vector<RigCamera> out;
		for (std::size_t c = 0; c < scene::scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{scene::identityOf(c)}, scene::model()});
		return out;
	}

	Report truthReport()
	{
		Report report;
		report.status = RegistrationStatus::Succeeded;
		const std::vector<math::RigidTransformd>& truth = scene::scene().referenceFromCamera;
		for (std::size_t c = 0; c < truth.size(); ++c)
			report.cameras.push_back({capture::CameraIdentity{scene::identityOf(c)}, truth[0].inverse() * truth[c]});
		return report;
	}
} // namespace

TEST_CASE("held-out tracks of a true registration are predicted to within their noise", "[camera][ceres][targetless]")
{
	ensureCeres();
	scene::reset(4, 200);
	const double sigma = 0.3; // pixels, per axis
	const feature::TrackSet set = scene::trackSetOf(sigma, 11);
	std::vector<std::string> ids;
	std::size_t members = 0;
	for (const feature::Track& t : set.tracks)
	{
		ids.push_back(t.identity);
		members += t.observations.size();
	}
	const auto result = validate(rig(), truthReport(), set, ids);
	REQUIRE(std::holds_alternative<HeldOutEvidence>(result));
	const HeldOutEvidence& e = std::get<HeldOutEvidence>(result);
	CHECK(e.predictions == members);
	CHECK(e.unpredicted == 0);
	// A member's observed pixel is off by the noise in two axes, and its prediction, from three noisy
	// others, adds its own error: the RMS pixel residual sits above sqrt(2) * sigma. Measured 0.587 px,
	// 1.38 times that, at 1.12 mrad RMS, the worst member 1.80 px out.
	CHECK(e.rmsPixels > std::sqrt(2.0) * sigma);
	CHECK(e.rmsPixels < 2 * std::sqrt(2.0) * sigma);
}
