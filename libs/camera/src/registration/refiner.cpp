#include "lain/camera/registration/refiner.h"

#include <memory>

namespace lain::camera::registration
{
	core::Factory<Refiner>& refinerRegistry()
	{
		static core::Factory<Refiner> registry;
		return registry;
	}

	bool canRefine()
	{
		return !refinerRegistry().keys().empty();
	}

	Solution refine(const Problem& problem)
	{
		const std::vector<std::string> backends = refinerRegistry().keys();
		if (backends.empty())
		{
			Solution out;
			out.status = RefinementStatus::NoBackend;
			out.detail = "this build has no registration refiner (configure with -DLAIN_CAMERA_CERES=ON)";
			return out;
		}
		const std::unique_ptr<Refiner> refiner = refinerRegistry().create(backends.front());
		Solution out = refiner->refine(problem);
		out.provenance = refiner->provenance();
		return out;
	}
} // namespace lain::camera::registration
