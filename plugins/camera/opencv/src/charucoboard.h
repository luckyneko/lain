#pragma once

// The one place a lain board pattern becomes an OpenCV board. Private to the plugin: no OpenCV type
// crosses into lain's camera contracts.

#include <lain/camera/board/pattern.h>

#include <opencv2/objdetect/aruco_board.hpp>
#include <opencv2/objdetect/aruco_dictionary.hpp>

namespace lain::camera::opencv
{
	cv::aruco::PredefinedDictionaryType toOpenCV(board::Dictionary dictionary);

	// The OpenCV board for `pattern` with `squareLength` per square (any unit; detection uses only
	// the ratio). Its marker ids run from the pattern's first id, and its layout is the pattern's.
	cv::aruco::CharucoBoard toCharucoBoard(const board::Pattern& pattern, float squareLength);
} // namespace lain::camera::opencv
