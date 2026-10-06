#pragma once

#include "lain/camera/rig.h"

// The cameras of a registration dataset, whichever method registers them. The types are
// lain::camera's (rig.h), since feature extraction takes them too; named here as well, where a
// registration's callers look for them.
namespace lain::camera::registration
{
	using camera::RigCamera;
	using camera::RigFootage;
} // namespace lain::camera::registration
