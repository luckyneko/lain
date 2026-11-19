#pragma once

#include "selection.h"

#include <lain/math/types.h>
#include <lain/render/viewport.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

#include <optional>
#include <vector>

namespace rigplan
{
	// An axis-aligned box in the plan frame.
	struct Bounds
	{
		lain::math::Vec3d lower{0.0};
		lain::math::Vec3d upper{0.0};
	};

	// What Frame All frames: every instance of `scene` (the room, the surfaces, the props, the boards'
	// sheets, the poles), every camera, light and fixed point `layout` places, and every key. None when
	// there is nothing at all.
	std::optional<Bounds> boundsOfEverything(const lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout,
											 const lain::rigplan::SceneResult& scene);

	// What Frame Selected frames: what `selection` draws (its instances, its camera's body or its
	// marker, a mount's pole and what is on it, a mover's sheet or mesh and its keys), at least half a
	// metre across, so a camera is not framed to fill the pane. Everything for the plan, a specification
	// or nothing; none when that is nothing either.
	std::optional<Bounds> boundsOfSelection(const lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout,
											const lain::rigplan::SceneResult& scene, const Selection& selection);

	// `scene` as the Viewport shows it from `eye`: a dollhouse, without each of the room's surfaces the eye
	// is outside of, so a room seen from beyond a wall is seen into rather than hidden by it. Every other
	// instance stays, with its owner. The room's centre, halfway up, says which side of a surface is in.
	lain::rigplan::SceneResult cutaway(const lain::rigplan::RigPlan& plan, const lain::rigplan::SceneResult& scene, const lain::math::Vec3d& eye);

	// Which of `scene`'s instances cutaway() leaves out, in its order: what changes it, so a view that
	// moves without crossing a wall needs no new scene.
	std::vector<bool> cutawayMask(const lain::rigplan::RigPlan& plan, const lain::rigplan::SceneResult& scene, const lain::math::Vec3d& eye);

	// The reference grid the Viewport lays on the plan frame's floor: lines every 0.1 m, a major one
	// every metre, over the room's floor, or 20 m each way of the origin without a room.
	lain::render::Grid floorGrid(const lain::rigplan::RigPlan& plan);
} // namespace rigplan
