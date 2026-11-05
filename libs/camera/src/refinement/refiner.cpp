#include "lain/camera/refinement/refiner.h"

#include <lain/string/format.h>

#include <algorithm>
#include <memory>

namespace lain::camera::refinement
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The distortion models a problem frees, each once, in the order the problem names them. An entry
	// past the problem's models is the backend's to refuse, not this one's to read.
	static std::vector<DistortionModel> freedModels(const Problem& problem)
	{
		std::vector<DistortionModel> out;
		const std::size_t count = std::min(problem.modelFreedom.size(), problem.models.size());
		for (std::size_t m = 0; m < count; ++m)
		{
			if (!problem.modelFreedom[m].frees(problem.models[m]))
				continue;
			const DistortionModel model = modelOf(problem.models[m].distortion());
			if (std::find(out.begin(), out.end(), model) == out.end())
				out.push_back(model);
		}
		return out;
	}

	// "Brown-Conrady 5", "Brown-Conrady 5 and Kannala-Brandt 4".
	static std::string namesOf(const std::vector<DistortionModel>& models)
	{
		std::string out;
		for (std::size_t i = 0; i < models.size(); ++i)
		{
			if (i > 0)
				out += i + 1 == models.size() ? " and " : ", ";
			out += displayName(models[i]);
		}
		return out;
	}

	// --- the seam -------------------------------------------------------------------------

	bool ModelFreedom::frees(const CameraModel& model) const
	{
		return focalLength || principalPoint || (coefficients && !camera::coefficients(model.distortion()).empty());
	}

	core::Factory<Refiner>& refinerRegistry()
	{
		static core::Factory<Refiner> registry;
		return registry;
	}

	bool canRefine()
	{
		return !refinerRegistry().keys().empty();
	}

	bool canEstimate(DistortionModel model)
	{
		for (const std::string& key : refinerRegistry().keys())
		{
			if (refinerRegistry().create(key)->canEstimate(model))
				return true;
		}
		return false;
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
		// The first backend that can estimate every model the problem frees, as board calibration picks
		// its estimator: one that cannot is never handed the problem.
		const std::vector<DistortionModel> freed = freedModels(problem);
		for (const std::string& key : backends)
		{
			const std::unique_ptr<Refiner> refiner = refinerRegistry().create(key);
			const bool able = std::all_of(freed.begin(), freed.end(),
										  [&](DistortionModel model)
										  { return refiner->canEstimate(model); });
			if (!able)
				continue;
			Solution out = refiner->refine(problem);
			out.provenance = refiner->provenance();
			return out;
		}
		Solution out;
		out.status = Status::Unsupported;
		out.detail = string::format("a refiner is registered, but none can estimate {}", namesOf(freed));
		return out;
	}
} // namespace lain::camera::refinement
