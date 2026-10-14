#pragma once

// A refiner stand-in for the registration method tests, board and targetless: it hands back the
// starting estimate, unrefined, so what a method module does around a refinement is the only thing
// under test, and it records what it was asked. The Ceres refiner has its own tests, in
// plugins/camera/ceres.

#include <lain/camera/refinement/refiner.h>

#include <cstddef>
#include <mutex>
#include <vector>

namespace lain::camera::testing
{
	// What the stand-in refiner was asked.
	struct RefinerScript
	{
		std::mutex mutex;
		struct Asked
		{
			bool freeCameras = true;
			std::size_t bodies = 0;
			std::size_t observations = 0;
			std::size_t landmarks = 0;
			std::size_t landmarkObservations = 0;
			std::size_t cameras = 0;
			refinement::RobustLoss loss;
			refinement::NoiseModel noise;
		};
		std::vector<Asked> asked;
	};
	inline RefinerScript& refinerScript()
	{
		static RefinerScript instance;
		return instance;
	}

	class PassThroughRefiner : public refinement::Refiner
	{
	public:
		Provenance provenance() const override { return {"passthrough", "1"}; }
		// It estimates nothing, so a problem that frees a model never reaches it.
		bool canEstimate(DistortionModel) const override { return false; }

		refinement::Solution refine(const refinement::Problem& problem) const override
		{
			{
				std::lock_guard<std::mutex> lock(refinerScript().mutex);
				refinerScript().asked.push_back({problem.freeCameras, problem.referenceFromBody.size(),
												 problem.observations.size(), problem.landmarks.size(),
												 problem.landmarkObservations.size(), problem.models.size(), problem.loss,
												 problem.noise});
			}
			refinement::Solution out;
			out.status = refinement::Status::Converged;
			out.cameraFromReference = problem.cameraFromReference;
			out.referenceFromBody = problem.referenceFromBody;
			out.landmarks = problem.landmarks;
			for (const CameraModel& model : problem.models)
				out.models.push_back(model.parameters());
			return out;
		}
	};

	inline void registerPassThroughRefiner()
	{
		static const bool once = []
		{
			refinement::refinerRegistry().registerType<PassThroughRefiner>("passthrough");
			return true;
		}();
		(void)once;
	}
} // namespace lain::camera::testing
