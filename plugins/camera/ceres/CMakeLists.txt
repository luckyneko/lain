# lain::camera::ceres — the Ceres-backed camera plugin (ADR-0017). Today only the probe proving the
# linked Ceres is the approved build, and that lain's projection differentiates through Ceres'
# automatic differentiation; the registration refiner behind lain::camera::registration's seam
# joins it in M9 slice 2, sub-slice 5. No Ceres or Eigen type crosses into lain's camera contracts.
#
# addCeres is included HERE, not at the root, so nothing is fetched or built unless this plugin is
# enabled — the rule addOpenCV follows for the OpenCV plugin.
#
# The namespace is lain::camera::ceres, as the OpenCV plugin's is lain::camera::opencv. Ceres' own
# namespace is ::ceres, so code inside lain::camera spells Ceres' types `::ceres::Problem`: an
# unqualified `ceres::` there finds this plugin's namespace first. A mistake is a compile error,
# never a silent wrong lookup.

include(addCeres) # Ceres::ceres + Eigen3::Eigen (fetches and builds Ceres on demand)
if(NOT LAIN_CERES_FOUND)
	return()
endif()

add_library(camera-ceres STATIC
	src/build.cpp
	src/register.cpp
)
add_library(lain::camera::ceres ALIAS camera-ceres)

target_compile_features(camera-ceres PUBLIC cxx_std_17)
target_include_directories(camera-ceres PUBLIC include)
target_link_libraries(camera-ceres
	PUBLIC lain::camera # it will be a registration Refiner, and registerBackend fills that registry
	PRIVATE Ceres::ceres
	PRIVATE lain::warnings
)

# The generated lain::camera::registerCameraBackends() discovers the plugin through this list and
# derives its <lain/camera/ceres/register.h> include and registerBackend() call from the name.
set_property(GLOBAL APPEND PROPERTY LAIN_CAMERA_BACKENDS lain::camera::ceres)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
