#pragma once

#include <lain/camera/backends.h>
#include <lain/io/data/codecs.h>
#include <lain/io/image/codecs.h>
#include <lain/io/sequence/openers.h>

namespace lain::camera::fixture
{
	// Everything a fixture needs registered, once per process: the camera backends, the image codecs
	// and sequence openers its footage opens through, and the json codec its documents are. One
	// function for every test file, since registering a codec twice is not something to rely on.
	inline void ensureRegistered()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::sequence::registerSequenceOpeners();
			io::data::registerDataCodecs();
			camera::registerCameraBackends();
			return true;
		}();
		(void)once;
	}
} // namespace lain::camera::fixture
