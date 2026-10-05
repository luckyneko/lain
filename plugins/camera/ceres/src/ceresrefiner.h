#pragma once

#include <lain/camera/registration/refiner.h>

namespace lain::camera::ceres
{
	// The global registration refinement through Ceres (ADR-0017): a sparse bundle adjustment of the
	// cameras and the latent rigid bodies, every residual through lain's own camera::project<T> so
	// what is minimised is exactly what the method module measures.
	//
	// Each camera and body is an angle-axis rotation plus a translation. The reference camera is held
	// constant, and every camera is in a pose-only problem. A residual is one corner seen by one
	// camera: its pixel error whitened by the corner's covariance, else by the noise model's sigma,
	// under the configured robust loss. SPARSE_SCHUR over Eigen's sparse Cholesky eliminates the
	// bodies first. Ceres runs on one thread, since lain supplies the parallelism (ADR-0024).
	class CeresRefiner : public registration::Refiner
	{
	public:
		Provenance provenance() const override;
		registration::Solution refine(const registration::Problem& problem) const override;
	};
} // namespace lain::camera::ceres
