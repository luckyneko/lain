// The linked Ceres is the build ADR-0017 approves: the pinned version, Eigen's sparse Cholesky, and
// none of the optional components that carry another licence or another dependency (SuiteSparse,
// METIS, LAPACK, CUDA, Accelerate), with its bundled miniglog rather than glog. Read from Ceres' own
// generated configuration, compiled into this binary, so it describes what was built rather than
// what cmake/addCeres.cmake asked for.

#include <lain/camera/ceres/build.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lain::camera::ceres;

TEST_CASE("the linked Ceres is the pinned version", "[ceres]")
{
	CHECK(version() == LAIN_CERES_PINNED_VERSION);
}

TEST_CASE("the linked Ceres has exactly the approved components", "[ceres]")
{
	const std::string configuration = lain::camera::ceres::configuration();
	CHECK(configuration.find("SuiteSparse: no\n") != std::string::npos);
	CHECK(configuration.find("Eigen sparse: yes\n") != std::string::npos);
	CHECK(configuration.find("METIS: no\n") != std::string::npos);
	CHECK(configuration.find("LAPACK: no\n") != std::string::npos);
	CHECK(configuration.find("CUDA: no\n") != std::string::npos);
	CHECK(configuration.find("Accelerate: no\n") != std::string::npos);
	CHECK(configuration.find("Schur specializations: no\n") != std::string::npos);
	CHECK(configuration.find("miniglog: yes\n") != std::string::npos);
}

TEST_CASE("Eigen is included without its LGPL code", "[ceres]")
{
	// Eigen3::Eigen carries the definition (cmake/addEigen.cmake), so every compile that sees
	// Eigen's headers has it: this one, and Ceres' own.
#ifdef EIGEN_MPL2_ONLY
	SUCCEED();
#else
	FAIL("EIGEN_MPL2_ONLY is not defined where Eigen is included");
#endif
}
