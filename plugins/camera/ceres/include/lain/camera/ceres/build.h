#pragma once

#include <string>

namespace lain::camera::ceres
{
	// What the LINKED Ceres was built with, read from its own generated configuration header rather
	// than inferred from the recipe that produced it (cmake/addCeres.cmake). No Ceres type crosses
	// this header: the plugin's public surface is lain's.

	// The version Ceres reports (CERES_VERSION_STRING), e.g. "2.2.0".
	std::string version();

	// The optional components this Ceres was built with or without, one "name: yes|no" per line:
	// SuiteSparse, Eigen sparse, METIS, LAPACK, CUDA, Accelerate, the fixed-size Schur
	// specialisations, and whether it logs through miniglog. Compiled into the binary that ships,
	// so it describes what was actually built.
	std::string configuration();
} // namespace lain::camera::ceres
