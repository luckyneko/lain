#include "lain/camera/ceres/build.h"

#include <ceres/internal/config.h>
#include <ceres/version.h>
// With MINIGLOG, Ceres puts miniglog's headers first on its include path, so this IS miniglog when
// Ceres was built with it; its include guard (spelled as Ceres spells it) says which one arrived.
#include <glog/logging.h>

namespace lain::camera::ceres
{
	std::string version()
	{
		return CERES_VERSION_STRING;
	}

	std::string configuration()
	{
		std::string out;
		const auto line = [&out](const char* name, bool built)
		{
			out += name;
			out += built ? ": yes\n" : ": no\n";
		};
#ifdef CERES_NO_SUITESPARSE
		line("SuiteSparse", false);
#else
		line("SuiteSparse", true);
#endif
#ifdef CERES_USE_EIGEN_SPARSE
		line("Eigen sparse", true);
#else
		line("Eigen sparse", false);
#endif
#ifdef CERES_NO_EIGEN_METIS
		line("METIS", false);
#else
		line("METIS", true);
#endif
#ifdef CERES_NO_LAPACK
		line("LAPACK", false);
#else
		line("LAPACK", true);
#endif
#ifdef CERES_NO_CUDA
		line("CUDA", false);
#else
		line("CUDA", true);
#endif
#ifdef CERES_NO_ACCELERATE_SPARSE
		line("Accelerate", false);
#else
		line("Accelerate", true);
#endif
#ifdef CERES_RESTRICT_SCHUR_SPECIALIZATION
		line("Schur specializations", false);
#else
		line("Schur specializations", true);
#endif
#ifdef CERCES_INTERNAL_MINIGLOG_GLOG_LOGGING_H_
		line("miniglog", true);
#else
		line("miniglog", false);
#endif
		return out;
	}
} // namespace lain::camera::ceres
