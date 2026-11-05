#pragma once

#include "lain/camera/calibration/fitness.h"
#include "lain/camera/cameramodel.h"

#include <lain/math/types.h>

#include <array>

// The image grid a calibration's coverage is counted over (calibration/fitness.h), whichever method's
// pixels reach it: a board's corners in board calibration, a frame's feature observations in
// targetless calibration. Private to lain::camera, so the calibration methods count it one way.
namespace lain::camera::detail
{
	// Which of the grid's cells something reaches, row by row.
	using CoverageCells = std::array<bool, calibration::kCoverageColumns * calibration::kCoverageRows>;

	// The cell `pixel` falls in, row by row, or -1 outside `image`.
	int coverageCell(const math::Vec2d& pixel, const ImageGeometry& image);

	// Marks the cell `pixel` falls in, when it falls in one.
	void cover(CoverageCells& cells, const math::Vec2d& pixel, const ImageGeometry& image);

	// The fraction of the grid's cells that `cells` reaches.
	double coveredFraction(const CoverageCells& cells);
} // namespace lain::camera::detail
