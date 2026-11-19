#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/math/types.h>
#include <lain/render/view.h>

namespace rigplan
{
	// The designer's own pinhole in the Viewport, as a turntable about a target: it orbits with +Z
	// always up, as a plan frame's floor is down. It is aimed as a rig plan aims a camera (Aim), with
	// the plan as its mount, so its yaw is a pan: at a yaw of 0 it looks along +Y, and a positive yaw
	// turns it left. Its pitch is how far it looks down at the target, so a positive pitch stands it
	// above.
	struct Orbit
	{
		lain::math::Vec3d target{0.0, 0.0, 1.0};
		double distance = 8;			// metres from the target
		double yawDegrees = 0;			// a pan about +Z
		double pitchDegrees = 25;		// down at the target; kept within kMaxPitchDegrees of level
		double verticalFovDegrees = 50; // the image's height, top to bottom

		// Short of looking straight down or up, where a turntable's yaw would stop meaning anything.
		static constexpr double kMaxPitchDegrees = 89.5;
		// How near and how far from the target it may stand, metres.
		static constexpr double kMinDistance = 0.02;
		static constexpr double kMaxDistance = 2000;
	};

	// The pinhole `orbit` is, for a pane `image` pixels in size: square pixels, the principal point at
	// the image's centre, and a depth range that follows its distance, so what it frames is drawn
	// however near or far that is.
	lain::render::View viewOf(const Orbit& orbit, lain::camera::ImageGeometry image);

	// Turns the orbit about its target: a yaw and a pitch in degrees, the pitch held short of straight up
	// or down.
	void orbitBy(Orbit& orbit, double yawDegrees, double pitchDegrees);

	// Slides the target in the image plane so that what was under the cursor at the target's distance
	// stays under it: `drag` is how far the cursor moved, in the pixels of a pane `image` in size.
	void panBy(Orbit& orbit, lain::math::Vec2d drag, lain::camera::ImageGeometry image);

	// Moves the eye toward the target (`amount` > 0) or away, the distance scaled by exp(-amount): the
	// same drag covers the same share of the way whatever the distance. Held within the orbit's bounds.
	void dollyBy(Orbit& orbit, double amount);

	// The scroll wheel's dolly: each step forward (> 0) takes a tenth of the way to the target.
	void zoomBy(Orbit& orbit, double steps);

	// The orbit turned as `orbit` is, its target the box's centre, and as near as puts every corner of
	// the axis-aligned box from `lower` to `upper` inside a pane `image` in size, with a margin: its
	// bounding sphere is fitted to the narrower of the two fields of view.
	Orbit framing(const Orbit& orbit, lain::math::Vec3d lower, lain::math::Vec3d upper, lain::camera::ImageGeometry image);
} // namespace rigplan
