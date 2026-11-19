#pragma once

// A rig of fixed cameras over a scene drawn per ray, for the rig's feature tracks measured against
// their truth (test_tracktruth.cpp): each camera's footage drawn by texturedscene.h when a frame is
// first asked for and kept, so footage extracted three ways is drawn once. The comparison of both
// registration methods renders a committed rig plan instead (test_comparison.cpp).

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/rig.h>
#include <lain/camera/testing/texturedscene.h>
#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lain::camera::rendered
{
	struct Rig
	{
		synthetic::Scene scene;
		CameraModel model;
		std::vector<capture::CameraIdentity> identities; // cam00, cam01, ...: ascending, as reports list them
		std::vector<math::RigidTransformd> worldFromCamera;
		std::vector<registration::RigFootage> footage;
		std::vector<capture::CaptureGroup> groups; // by position
	};

	// `scene` seen through `model` from each of `poses` for `frames` frames, each pixel the mean of
	// `samples` x `samples` rays. Camera c's noise is seeded c + 1.
	inline Rig rig(const synthetic::Scene& scene, const CameraModel& model, const std::vector<math::RigidTransformd>& poses,
				   std::size_t frames, int samples = 2)
	{
		Rig r{scene, model, {}, poses, {}, {}};
		std::vector<capture::CameraFootage> byPosition;
		for (std::size_t c = 0; c < poses.size(); ++c)
		{
			const std::string name = (c < 10 ? "cam0" : "cam") + std::to_string(c);
			r.identities.push_back(capture::CameraIdentity{name});
			r.footage.push_back({r.identities.back(), model,
								 media::FrameSequence::over(std::make_shared<synthetic::RenderedSource>(
									 name, scene, model, poses[c], frames, std::uint64_t(c + 1), samples))});
			byPosition.push_back({r.identities.back(), r.footage.back().footage});
		}
		const capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
		REQUIRE(grouping.groups.size() == frames);
		r.groups = grouping.groups;
		return r;
	}

	inline std::vector<registration::RigCamera> camerasOf(const Rig& r)
	{
		std::vector<registration::RigCamera> out;
		for (const registration::RigFootage& f : r.footage)
			out.push_back({f.camera, f.model});
		return out;
	}
} // namespace lain::camera::rendered
