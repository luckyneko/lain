#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Refinement (CONTEXT.md, "Global registration refinement"): the seam a solver backend fills
// (ADR-0017). Everything here is lain's, so the Ceres plugin's types stay inside the plugin. The
// problem is CAMERAS plus two kinds of latent unknown, either or both: RIGID BODIES with known points
// (a board in one capture group is a body, its corners the points) and LANDMARKS, scene points whose
// positions are themselves unknown (a feature track's point). One problem, one path, whichever the
// method observes.
//
// It is lain::camera's, not a method module's, because registration and calibration both refine and
// neither may depend on the other (ADR-0016). So this header includes nothing from registration/ or
// calibration/, and must not start to: a seam that named one method's request would put the other
// method downstream of it. Registration names these types where its callers look for them
// (registration/refiner.h, registration/request.h).
namespace lain::camera::refinement
{
	// The pixel noise an observation with no measured covariance is assumed to have (CONTEXT.md,
	// "Observation-noise model"). A detector's response is never turned into this.
	struct NoiseModel
	{
		double pixelSigma = 0.5;
	};

	enum class LossFamily
	{
		None,	// least squares
		Huber,	// quadratic within the scale, linear beyond it
		Cauchy, // logarithmic beyond the scale: an outlier's pull fades
	};

	// The robust loss on each whitened residual, which bounds an outlier's influence. The scale, in
	// standard deviations, is also where an observation counts as an outlier in the report.
	//
	// Cauchy by default, because the outlier a board registration meets is a whole view that does not
	// belong: a frame from another instant, its corners tens of standard deviations off. Huber's pull
	// stays linear out there, and a view of 24 corners still drags the rig: measured on the test rig,
	// one such view at 66 sigma moved a camera 8.4 mrad under Huber and 1.6 under Cauchy, against 1.8
	// with no stray view at all. Cauchy's redescends; its non-convexity is safe because
	// initialisation, which a stray view cannot steer, starts the refinement close.
	struct RobustLoss
	{
		LossFamily family = LossFamily::Cauchy;
		double scale = 3.0;
	};

	// One corner seen by one camera: the point in its body's frame, and where the camera saw it.
	struct BodyObservation
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
		// As BodyObservation's: measured, or the problem's NoiseModel.
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
		std::vector<BodyObservation> observations;
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

	enum class Status
	{
		Converged,		   // the solver's tolerances were met
		IterationLimit,	   // usable, but stopped at the iteration limit
		Failed,			   // no usable solution
		ResourceExhausted, // the machine ran out of memory for this problem
		NoBackend,		   // this build has no refiner
	};

	struct Solution
	{
		Status status = Status::Failed;
		std::string detail; // the solver's own account, or why there is no solution
		std::vector<math::RigidTransformd> cameraFromReference;
		std::vector<math::RigidTransformd> referenceFromBody;
		std::vector<math::Vec3d> landmarks; // in the reference frame
		std::uint32_t iterations = 0;
		double initialCost = 0; // half the sum of squared, whitened, loss-applied residuals
		double finalCost = 0;
		core::Time elapsed;
		Provenance provenance;

		bool usable() const { return status == Status::Converged || status == Status::IterationLimit; }
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

	// Whether this build can refine at all: whether any backend registered.
	bool canRefine();

	// Refine through the registered backend. With none registered the solution is NoBackend.
	Solution refine(const Problem& problem);
} // namespace lain::camera::refinement
