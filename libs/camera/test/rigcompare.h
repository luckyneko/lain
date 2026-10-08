#pragma once

// Comparing registered rigs, for the tests that register real footage: one rig against its truth, or
// one method's result against another's. Every rig here is relative to one reference camera, which
// each method pins to the identity, so two results of the same rig can differ in rotation and in
// the scale of their centres and in nothing else: a targetless registration is scale-normalised,
// a board registration metric. A scale-only fit is therefore the whole alignment, and stricter than
// a similarity fit, which could hide a reference camera's own error.

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/report.h>
#include <lain/math/rigidtransform.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lain::camera::testing
{
	// The angle between two poses' rotations, radians.
	inline double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd r = math::conjugate(a.rotation()) * b.rotation();
		return 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w));
	}

	// The registered pose of each of `identities`, in that order. Each must be registered.
	inline std::vector<math::RigidTransformd> posesOf(const registration::Report& report,
													  const std::vector<capture::CameraIdentity>& identities)
	{
		std::vector<math::RigidTransformd> poses;
		for (const capture::CameraIdentity& id : identities)
		{
			const std::optional<math::RigidTransformd> pose = report.referenceFromCamera(id);
			REQUIRE(pose.has_value());
			poses.push_back(*pose);
		}
		return poses;
	}

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

	struct RigDifference
	{
		double scale = 1;			  // what `found`'s centres were multiplied by
		std::vector<double> rotation; // per camera, radians
		std::vector<double> centre;	  // per camera, in `expected`'s units
		double worstRotation = 0;
		double worstCentre = 0;
	};

	// How far `found` is from `expected`, both relative to the same reference camera: rotations
	// directly, centres after `found`'s are scaled onto `expected`'s by least squares, s = sum f.e /
	// sum f.f, the bootstrap's own alignment (targetless.cpp).
	inline RigDifference compare(const std::vector<math::RigidTransformd>& expected,
								 const std::vector<math::RigidTransformd>& found)
	{
		REQUIRE(expected.size() == found.size());
		double across = 0, along = 0;
		for (std::size_t c = 0; c < expected.size(); ++c)
		{
			across += math::dot(found[c].translation(), expected[c].translation());
			along += math::dot(found[c].translation(), found[c].translation());
		}
		RigDifference d;
		d.scale = along > 0 ? across / along : 1.0;
		for (std::size_t c = 0; c < expected.size(); ++c)
		{
			d.rotation.push_back(rotationBetween(expected[c], found[c]));
			d.centre.push_back(math::length(found[c].translation() * d.scale - expected[c].translation()));
			d.worstRotation = std::max(d.worstRotation, d.rotation.back());
			d.worstCentre = std::max(d.worstCentre, d.centre.back());
		}
		return d;
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
