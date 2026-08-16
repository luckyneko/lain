# Use Ceres for registration refinement

Board and targetless fixed-camera registration use Ceres as an optional private nonlinear
optimization dependency. Public camera APIs expose lain-owned observations, transforms, requests,
reports, and diagnostics; they do not expose Ceres or Eigen types.

The initial supported configuration disables SuiteSparse and uses Ceres' Eigen sparse or iterative
Schur solvers. This keeps mixed GPL/commercial SuiteSparse components out of the dependency graph
while retaining the block-sparse optimization needed by the large-rig design target. Any future
SuiteSparse configuration requires a separate license decision.

Eigen's MPL-2.0 license is an approved, scoped exception to the permissive-by-default dependency
policy. Camera builds define `EIGEN_MPL2_ONLY`, keep Eigen types private, and follow the source and
notice obligations recorded in ADR-0015 and the repository third-party license inventory.

## Consequences

Ceres, Eigen, and Abseil are paid for only when a registration implementation that needs global
refinement is enabled. Ceres may be linked statically or dynamically according to the host build, but
remains an implementation detail either way.

The root `README.md` records Ceres, Eigen, and Abseil as approved planned dependencies before their
build integration lands. Their exact versions and enabled configurations replace that planned status
when the implementation is added; removing or replacing one updates the inventory in the same change.

Registration has one production global-refinement path rather than separate dense Eigen and Ceres
implementations. It preserves sparse residual structure, does not encode small fixed camera or
capture-group limits, and releases decoded image data after producing the compact observations used
by optimization. Resource exhaustion is reported through the registration report rather than
represented as an artificial API limit.

Global refinement whitens observations using measured pixel covariance when available. Otherwise it
uses a configured uniform pixel-noise assumption and a configured robust loss; detector response does
not silently modify residual weight. Reports retain the assumed noise, loss family and scale, and
outlier diagnostics so weighting decisions are reproducible.
