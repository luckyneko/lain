#pragma once

#include <lain/camera/board/pose.h>

namespace lain::camera::opencv
{
	// The board pose solver behind lain::camera::board's seam: OpenCV's IPPE, which is exact for a
	// planar target and returns both of a plane's poses, each then refined by Levenberg-Marquardt.
	// It solves on normalised coordinates unprojected through lain's own model, so it works for
	// every distortion model, including the ones OpenCV cannot represent.
	class OpenCVPoseSolver : public board::PoseSolver
	{
	public:
		Provenance provenance() const override;
		std::vector<math::RigidTransformd> solve(const CameraModel& model, const board::Specification& board,
												 const board::Observation& view) const override;
	};
} // namespace lain::camera::opencv
