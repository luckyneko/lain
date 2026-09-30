#pragma once

#include <lain/camera/board/detection.h>

namespace lain::camera::opencv
{
	// ChArUco detection with OpenCV: markers found at the requested scale, chessboard corners
	// interpolated from them, mapped back to source pixels, and then (by default) refined against
	// native-resolution pixels.
	class CharucoDetector : public board::Detector
	{
	public:
		board::DetectionReport detect(const image::Image& image, const media::FrameRef& frame,
									  const board::Specification& board, const board::DetectionRequest& request,
									  double scale) const override;
	};
} // namespace lain::camera::opencv
