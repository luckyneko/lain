#include "coverage.h"

#include <algorithm>
#include <cstddef>

namespace lain::camera::detail
{
	int coverageCell(const math::Vec2d& pixel, const ImageGeometry& image)
	{
		const double u = (pixel.x + 0.5) / double(image.width);
		const double v = (pixel.y + 0.5) / double(image.height);
		if (!(u >= 0 && u < 1 && v >= 0 && v < 1))
			return -1;
		return int(v * calibration::kCoverageRows) * calibration::kCoverageColumns + int(u * calibration::kCoverageColumns);
	}

	void cover(CoverageCells& cells, const math::Vec2d& pixel, const ImageGeometry& image)
	{
		const int cell = coverageCell(pixel, image);
		if (cell >= 0)
			cells[std::size_t(cell)] = true;
	}

	double coveredFraction(const CoverageCells& cells)
	{
		return double(std::count(cells.begin(), cells.end(), true)) / double(cells.size());
	}
} // namespace lain::camera::detail
