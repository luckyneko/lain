#include "lain/camera/opencv/build.h"

#include <opencv2/core/utility.hpp>

namespace lain::camera::opencv
{
	std::string version()
	{
		return cv::getVersionString();
	}

	std::string buildInformation()
	{
		return cv::getBuildInformation();
	}
} // namespace lain::camera::opencv
