#pragma once

// A rig of fixed cameras over a rendered scene, for the end-to-end registration tests: each camera's
// footage drawn by texturedscene.h when a frame is first asked for and kept, so the same footage
// registered by two methods is drawn once.

#include "texturedscene.h"

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/feature/tracks.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/rig.h>
#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

	// Where the camera at `worldFromCamera` sees `point`, or nothing.
	inline std::optional<math::Vec2d> projectInto(const CameraModel& m, const math::RigidTransformd& worldFromCamera,
												  const math::Vec3d& point)
	{
		const math::Vec3d p = worldFromCamera.inverse().apply(point);
		if (p.z <= 0)
			return std::nullopt;
		const Projection<double> px = project(m, p.x, p.y, p.z);
		if (!px.ok())
			return std::nullopt;
		return math::Vec2d{px.u, px.v};
	}

	struct StillScene
	{
		std::size_t off = 0; // tracks that are no point of the still scene
		double worst = 0;	 // pixels, the furthest a member of any other track lands from its point
	};

	// Whether `tracks` are points of the scene without its moving content: each track's first member
	// cast into the still scene, and the point found there projected into its other members' cameras.
	// A track off by more than `tolerance` px is moving content that got in. Validation against the
	// true rig cannot tell: a synchronised rig sees a moving feature at one instant from every camera,
	// which is a consistent point at that instant. The still scene can, since behind the board or the
	// disc is a wall a metre or more further away.
	inline StillScene stillScene(const Rig& r, const feature::TrackSet& tracks, double tolerance = 20.0)
	{
		// Built from the planes rather than copied and cleared: GCC 13 at -O3 warns of a cleared
		// optional board as maybe uninitialised.
		synthetic::Scene still;
		still.planes = r.scene.planes;
		still.background = r.scene.background;
		StillScene out;
		for (const feature::Track& t : tracks.tracks)
		{
			const feature::SceneObservation& first = t.observations.front();
			const std::optional<synthetic::Hit> hit = synthetic::truthAt(still, r.model, r.worldFromCamera[first.view], first.pixel);
			double furthest = hit ? 0.0 : 1e9;
			for (std::size_t o = 1; o < t.observations.size() && hit; ++o)
			{
				const feature::SceneObservation& other = t.observations[o];
				const std::optional<math::Vec2d> p = projectInto(r.model, r.worldFromCamera[other.view], hit->point);
				furthest = std::max(furthest, p ? math::length(*p - other.pixel) : 1e9);
			}
			if (furthest > tolerance)
				++out.off;
			else
				out.worst = std::max(out.worst, furthest);
		}
		return out;
	}
} // namespace lain::camera::rendered
