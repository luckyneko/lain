#pragma once

#include <lain/camera/refinement/refiner.h>

namespace lain::camera::ceres
{
	// Refinement through Ceres (ADR-0017): a sparse bundle adjustment of the cameras, the latent rigid
	// bodies and landmarks, and any free camera models, every residual through lain's own
	// camera::project<T> so what is minimised is exactly what the method module measures.
	//
	// Each camera and body is an angle-axis rotation plus a translation, and each landmark a point.
	// The reference camera is held constant, and every camera is in a pose-only problem. A free model
	// is one block, [fx, fy, cx, cy, coefficients...] in the parameter overload's order, whose held
	// fields sit on a SubsetManifold; a held model has no block. A residual is one corner or landmark
	// seen by one camera: its pixel error whitened by the observation's covariance, else by the noise
	// model's sigma, under the configured robust loss. The linear solver follows the problem's shape:
	// SPARSE_SCHUR over Eigen's sparse Cholesky, eliminating the bodies and landmarks first, when
	// anything they share is free. Ceres runs on one thread, since lain supplies the parallelism
	// (ADR-0024).
	class CeresRefiner : public refinement::Refiner
	{
	public:
		Provenance provenance() const override;
		bool canEstimate(DistortionModel model) const override;
		refinement::Solution refine(const refinement::Problem& problem) const override;
	};
} // namespace lain::camera::ceres
