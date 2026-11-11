#pragma once

#include "selection.h"

#include <lain/math/types.h>
#include <lain/render/raycast.h>
#include <lain/render/view.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

namespace rigplan
{
	// A ray in the plan frame, which the scene it is cast into is in.
	using lain::render::Ray;

	// The ray through pixel `pixel` of `view`'s pinhole, in lain's pixel convention ((0, 0) the centre of
	// the top-left pixel), from the view's optical centre.
	Ray rayThrough(const lain::render::View& view, lain::math::Vec2d pixel);

	// What a click along `ray` selects: the nearest thing it meets of
	//   * every instance of `scene` but the room's (a plane, a box's faces, a prop, a board's sheet, a
	//     pole, which selects its mount);
	//   * each camera's body, a box of its sensor body's size about its optical centre (a camera with no
	//     sensor specification is given SensorBody's default size);
	//   * each light's, key's and fixed point's marker, a sphere of kMarkerRadius;
	// and only when it meets none of them, the room. Nothing when it meets nothing. `layout` and `scene`
	// are `plan`'s, as the Viewport draws them, so what is picked is what is under the cursor.
	Selection pick(const lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout, const lain::rigplan::SceneResult& scene,
				   const Ray& ray);
} // namespace rigplan
