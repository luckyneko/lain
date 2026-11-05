#include "lain/camera/ceres/register.h"

#include "ceresrefiner.h"

namespace lain::camera::ceres
{
	void registerBackend()
	{
		refinement::refinerRegistry().registerType<CeresRefiner>("ceres");
	}
} // namespace lain::camera::ceres
