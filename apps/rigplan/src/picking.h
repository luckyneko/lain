#pragma once

#include "selection.h"

#include <lain/math/types.h>
#include <lain/render/view.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

#include <optional>

namespace rigplan
{
	// A ray in the plan frame: from `origin`, along the unit `direction`.
	struct Ray
	{
		lain::math::Vec3d origin{0.0, 0.0, 0.0};
		lain::math::Vec3d direction{0.0, 0.0, 1.0};
	};

	// The ray through pixel `pixel` of `view`'s pinhole, in lain's pixel convention ((0, 0) the centre of
	// the top-left pixel), from the view's optical centre.
	Ray rayThrough(const lain::render::View& view, lain::math::Vec2d pixel);

	// The distance along `ray` to the nearest point of the triangle (a, b, c) it meets from in front, or
	// none.
	std::optional<double> hitTriangle(const Ray& ray, const lain::math::Vec3d& a, const lain::math::Vec3d& b, const lain::math::Vec3d& c);

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
