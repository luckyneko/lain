#pragma once

// What the OpenCV feature cases share (test_features.cpp, test_sequencefeatures.cpp) beyond
// lain::camera::testing: the backend, registered once. A camera of any size is testcamera.h's
// largeCamera, and features grouped and capped as extraction groups and caps them are
// featurecells.h's, where bench-camera-opencv reaches them too.

#include <lain/camera/opencv/register.h>
#include <lain/camera/testing/featurecells.h>
#include <lain/camera/testing/testcamera.h>

namespace lain::camera::testing
{
	inline void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}
} // namespace lain::camera::testing
