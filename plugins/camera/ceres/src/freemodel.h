#pragma once

// A FREE camera model in the Ceres refiner: its parameter block and the residuals that read it.
// Private to the plugin, and its own file so the held path's inlining is not starved (observe.h).

#include "observe.h"

#include <lain/camera/cameramodel.h>
#include <lain/camera/refinement/refiner.h>

#include <ceres/cost_function.h>

#include <cstddef>
#include <vector>

namespace lain::camera::ceres
{
	// A model as its free parameter block, [fx, fy, cx, cy, coefficients...] in the parameter
	// overload's order, and back. The block's held fields come back unchanged, since a SubsetManifold
	// never moves them; the result is UNCHECKED, for the method to pass through CameraModel::create.
	std::vector<double> blockOf(const CameraModel& model);
	CameraModelParameters parametersOf(const CameraModel& start, const std::vector<double>& block);

	// The fields of a free block that `freedom` holds, for its SubsetManifold.
	std::vector<int> heldFields(const refinement::ModelFreedom& freedom, std::size_t blockSize);

	// The cost of one corner, or one landmark, seen through a free model, for the model type `start`
	// holds. Each projects through the STARTING model's domain (ADR-0017). Blocks: the camera, the
	// body or landmark, then the model.
	::ceres::CostFunction* freeCornerCost(const CameraModel& start, const math::Vec3d& point, const math::Vec2d& pixel,
										  const Whitening& w);
	::ceres::CostFunction* freePointCost(const CameraModel& start, const math::Vec2d& pixel, const Whitening& w);
} // namespace lain::camera::ceres
