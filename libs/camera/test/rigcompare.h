#pragma once

// The truth, as a registered rig, for the tests that register rendered footage. What a result is
// scored by is the library's rig comparison (lain/camera/comparison.h): camera::compareRigs against
// the truthRig built here. What is left here builds the truth from the world poses a test drew its
// footage from, which a rig plan's capture does from its poses (rigplan's rigRelativeTo).

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/report.h>
#include <lain/math/rigidtransform.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace lain::camera::testing
{
	// Poses given in some world frame, made relative to camera `reference`: what a registration
	// reports for them.
	inline std::vector<math::RigidTransformd> rebased(const std::vector<math::RigidTransformd>& worldFromCamera,
													  std::size_t reference)
	{
		const math::RigidTransformd referenceFromWorld = worldFromCamera[reference].inverse();
		std::vector<math::RigidTransformd> out;
		for (const math::RigidTransformd& pose : worldFromCamera)
			out.push_back(referenceFromWorld * pose);
		return out;
	}

	// The truth as a registered rig: `identities[c]` at `worldFromCamera[c]`, relative to `reference`,
	// metric, cameras ascending by identity as a registration reports them. The reference is the
	// identity exactly, as a rig document requires, rather than a product that rounds to it. Build it
	// for the reference the registration chose, so compareRigs has nothing to derive again.
	inline registration::RegisteredRig truthRig(const std::vector<capture::CameraIdentity>& identities,
												const std::vector<math::RigidTransformd>& worldFromCamera,
												const capture::CameraIdentity& reference)
	{
		REQUIRE(identities.size() == worldFromCamera.size());
		const auto found = std::find(identities.begin(), identities.end(), reference);
		REQUIRE(found != identities.end());
		const std::size_t r = std::size_t(found - identities.begin());
		const std::vector<math::RigidTransformd> poses = rebased(worldFromCamera, r);

		registration::RegisteredRig rig;
		rig.reference = reference;
		rig.scale = {registration::Scale::Metric, "the poses the test's footage was drawn from"};
		for (std::size_t c = 0; c < identities.size(); ++c)
			rig.cameras.push_back({identities[c], c == r ? math::RigidTransformd{} : poses[c]});
		std::sort(rig.cameras.begin(), rig.cameras.end(),
				  [](const registration::RegisteredCamera& a, const registration::RegisteredCamera& b)
				  { return a.camera < b.camera; });
		return rig;
	}

	// A successful report holding nothing but `identities` at `poses`, which is all
	// registration::validate reads: the truth scored as if a method had registered it.
	inline registration::Report truthReport(const std::vector<capture::CameraIdentity>& identities,
											const std::vector<math::RigidTransformd>& poses)
	{
		registration::Report report;
		report.status = registration::RegistrationStatus::Succeeded;
		for (std::size_t c = 0; c < identities.size(); ++c)
			report.cameras.push_back({identities[c], poses[c]});
		std::sort(report.cameras.begin(), report.cameras.end(),
				  [](const registration::RegisteredCamera& a, const registration::RegisteredCamera& b)
				  { return a.camera < b.camera; });
		return report;
	}
} // namespace lain::camera::testing
