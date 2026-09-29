#pragma once

#include <string>

namespace lain::camera::opencv
{
	// What the LINKED OpenCV reports about itself, asked through its own API rather than inferred
	// from the recipe that produced it.
	//
	// cmake/addOpenCV.cmake gates on the prebuilt archive's MANIFEST.txt, which needs no execution
	// and so survives cross-compiling; these are the runtime second opinion, and the only gate at
	// all for an OpenCV someone supplied via LAIN_OPENCV_ROOT (ADR-0026). No OpenCV type crosses
	// this header: the plugin's public surface is lain's, now and when the calibration adapters
	// land beside it.

	// The version the library reports (cv::getVersionString), e.g. "4.14.0".
	std::string version();

	// The library's own build report (cv::getBuildInformation): modules, compiler, third-party
	// components, parallel framework. Compiled into the binary that ships, so it describes what
	// was actually built rather than what a configure log said it intended.
	std::string buildInformation();
} // namespace lain::camera::opencv
