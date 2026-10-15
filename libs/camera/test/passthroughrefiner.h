#pragma once

// A refiner stand-in for the registration and targetless calibration tests: it hands back the starting
// estimate, unrefined, so what a method module does around a refinement is the only thing under test,
// and it records what it was asked. It can be told it estimates a model, and a model a problem frees
// comes back as it went in. The Ceres refiner has its own tests, in plugins/camera/ceres.

#include <lain/camera/refinement/refiner.h>

#include <cstddef>
#include <mutex>
#include <set>
#include <utility>
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
			std::size_t poses = 0;								// cameraFromReference
			std::vector<refinement::ModelFreedom> modelFreedom; // as the problem gave it
			std::vector<math::Vec2d> pixels;					// every landmark observation's
		};
		std::vector<Asked> asked;
		// The models it says it can estimate: none unless a case says so.
		std::set<DistortionModel> estimates;
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
		// The models the script says, none by default: a problem that frees another never reaches it.
		bool canEstimate(DistortionModel model) const override
		{
			std::lock_guard<std::mutex> lock(refinerScript().mutex);
			return refinerScript().estimates.count(model) != 0;
		}

		refinement::Solution refine(const refinement::Problem& problem) const override
		{
			{
				std::lock_guard<std::mutex> lock(refinerScript().mutex);
				std::vector<math::Vec2d> pixels;
				for (const refinement::LandmarkObservation& o : problem.landmarkObservations)
					pixels.push_back(o.pixel);
				refinerScript().asked.push_back({problem.freeCameras, problem.referenceFromBody.size(),
												 problem.observations.size(), problem.landmarks.size(),
												 problem.landmarkObservations.size(), problem.models.size(), problem.loss,
												 problem.noise, problem.cameraFromReference.size(), problem.modelFreedom,
												 std::move(pixels)});
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
