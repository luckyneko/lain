#include "lain/camera/refinement/refiner.h"

#include <memory>

namespace lain::camera::refinement
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
			out.status = Status::NoBackend;
			out.detail = "this build has no refiner (configure with -DLAIN_CAMERA_CERES=ON)";
			return out;
		}
		const std::unique_ptr<Refiner> refiner = refinerRegistry().create(backends.front());
		Solution out = refiner->refine(problem);
		out.provenance = refiner->provenance();
		return out;
	}
} // namespace lain::camera::refinement
