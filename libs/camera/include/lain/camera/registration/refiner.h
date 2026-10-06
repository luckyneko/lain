#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/provenance.h"
#include "lain/camera/registration/request.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Global registration refinement (CONTEXT.md): the seam a solver backend fills (ADR-0017). Everything
// here is lain's, so the Ceres plugin's types stay inside the plugin. The problem is CAMERAS plus two
// kinds of latent unknown, either or both: RIGID BODIES with known points (a board in one capture
// group is a body, its corners the points) and LANDMARKS, scene points whose positions are themselves
// unknown (a feature track's point). One problem, one path, whichever the method observes.
namespace lain::camera::registration
{
	// One corner seen by one camera: the point in its body's frame, and where the camera saw it.
	struct RefinementObservation
	{
		std::uint32_t camera = 0;
		std::uint32_t body = 0;
		math::Vec3d point{0.0};
		math::Vec2d pixel{0.0};
		// Measured pixel covariance (xx, xy, yy), only when a detector could defend one; otherwise
		// the problem's NoiseModel applies.
		std::optional<std::array<double, 3>> covariance;
	};

	// One landmark seen by one camera: where the camera saw it. A landmark is one unknown point, so
	// an observation names it rather than carrying a point.
	struct LandmarkObservation
	{
		std::uint32_t camera = 0;
		std::uint32_t landmark = 0;
		math::Vec2d pixel{0.0};
		// As RefinementObservation's: measured, or the problem's NoiseModel.
		std::optional<std::array<double, 3>> covariance;
	};

	struct Problem
	{
		std::vector<CameraModel> models;						// per camera, held fixed
		std::vector<math::RigidTransformd> cameraFromReference; // per camera: the starting estimate
		std::uint32_t reference = 0;							// held at its starting transform
		// False holds EVERY camera, so only the bodies and landmarks move: a pose-only solve, which
		// held-out validation uses to place a board from the cameras that saw it, or a triangulation,
		// which places a held-out track's point from the cameras that saw it.
		bool freeCameras = true;
		std::vector<math::RigidTransformd> referenceFromBody; // per body: the starting estimate
		std::vector<RefinementObservation> observations;
		// Per landmark: the starting estimate, in the reference frame. Every landmark must be in front
		// of every camera observing it at the start: a projection that fails there fails the solve.
		//
		// Nothing here fixes the scale when nothing metric is observed: a problem of landmarks alone
		// is solved with its scale free, and normalising it is the method's (ADR-0017).
		std::vector<math::Vec3d> landmarks;
		std::vector<LandmarkObservation> landmarkObservations;
		NoiseModel noise;
		RobustLoss loss;
		std::uint32_t maximumIterations = 100;
	};

	enum class RefinementStatus
	{
		Converged,		   // the solver's tolerances were met
		IterationLimit,	   // usable, but stopped at the iteration limit
		Failed,			   // no usable solution
		ResourceExhausted, // the machine ran out of memory for this problem
		NoBackend,		   // this build has no refiner
	};

	struct Solution
	{
		RefinementStatus status = RefinementStatus::Failed;
		std::string detail; // the solver's own account, or why there is no solution
		std::vector<math::RigidTransformd> cameraFromReference;
		std::vector<math::RigidTransformd> referenceFromBody;
		std::vector<math::Vec3d> landmarks; // in the reference frame
		std::uint32_t iterations = 0;
		double initialCost = 0; // half the sum of squared, whitened, loss-applied residuals
		double finalCost = 0;
		core::Time elapsed;
		Provenance provenance;

		bool usable() const { return status == RefinementStatus::Converged || status == RefinementStatus::IterationLimit; }
	};

	// A refinement backend (ADR-0004's service shape). It moves cameras, bodies and landmarks to minimise the
	// whitened, robustly weighted reprojection error through lain's own projection; what the result
	// means is the method module's.
	class Refiner
	{
	public:
		virtual ~Refiner() = default;

		virtual Provenance provenance() const = 0;
		virtual Solution refine(const Problem& problem) const = 0;
	};

	// The process-wide refiner registry, keyed by backend name ("ceres").
	core::Factory<Refiner>& refinerRegistry();

	// Whether this build can refine a registration at all: whether any backend registered.
	bool canRefine();

	// Refine through the registered backend. With none registered the solution is NoBackend.
	Solution refine(const Problem& problem);
} // namespace lain::camera::registration
