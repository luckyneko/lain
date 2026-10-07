#pragma once

// A refiner stand-in for the registration method tests, board and targetless: it hands back the
// starting estimate, unrefined, so what a method module does around a refinement is the only thing
// under test, and it records what it was asked. The Ceres refiner has its own tests, in
// plugins/camera/ceres.

#include <lain/camera/registration/refiner.h>

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
			registration::RobustLoss loss;
			registration::NoiseModel noise;
		};
		std::vector<Asked> asked;
	};
	inline RefinerScript& refinerScript()
	{
		static RefinerScript instance;
		return instance;
	}

	class PassThroughRefiner : public registration::Refiner
	{
	public:
		Provenance provenance() const override { return {"passthrough", "1"}; }

		registration::Solution refine(const registration::Problem& problem) const override
		{
			{
				std::lock_guard<std::mutex> lock(refinerScript().mutex);
				refinerScript().asked.push_back({problem.freeCameras, problem.referenceFromBody.size(),
												 problem.observations.size(), problem.landmarks.size(),
												 problem.landmarkObservations.size(), problem.models.size(), problem.loss,
												 problem.noise});
			}
			registration::Solution out;
			out.status = registration::RefinementStatus::Converged;
			out.cameraFromReference = problem.cameraFromReference;
			out.referenceFromBody = problem.referenceFromBody;
			out.landmarks = problem.landmarks;
			return out;
		}
	};

	inline void registerPassThroughRefiner()
	{
		static const bool once = []
		{
			registration::refinerRegistry().registerType<PassThroughRefiner>("passthrough");
			return true;
		}();
		(void)once;
	}
} // namespace lain::camera::testing
