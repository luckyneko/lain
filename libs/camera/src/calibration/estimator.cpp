#include "lain/camera/calibration/estimator.h"

namespace lain::camera::calibration
{
	core::Factory<Estimator>& estimatorRegistry()
	{
		static core::Factory<Estimator> registry;
		return registry;
	}

	bool canEstimate(DistortionModel model)
	{
		for (const std::string& key : estimatorRegistry().keys())
		{
			if (estimatorRegistry().create(key)->canEstimate(model))
				return true;
		}
		return false;
	}

	bool canEstimate()
	{
		return !estimatorRegistry().keys().empty();
	}
} // namespace lain::camera::calibration
