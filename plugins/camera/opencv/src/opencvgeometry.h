#pragma once

#include <lain/camera/feature/geometry.h>

namespace lain::camera::opencv
{
	// Candidate poses behind lain::camera::feature's geometry seam: OpenCV's USAC with MAGSAC++
	// scoring for an essential matrix, every one of its four decompositions, and a USAC PnP for an
	// absolute pose. Run serially and seeded, so the same input proposes the same candidates. It
	// proposes; lain measures every candidate and chooses (ADR-0016).
	class OpenCVGeometrySolver : public feature::GeometrySolver
	{
	public:
		Provenance provenance() const override;
		std::vector<math::RigidTransformd> relativePoses(const std::vector<feature::RayPair>& pairs, double angle,
														 std::uint64_t seed) const override;
		std::vector<math::RigidTransformd> absolutePoses(const std::vector<feature::PointRay>& pointRays, double angle,
														 std::uint64_t seed) const override;
	};
} // namespace lain::camera::opencv
