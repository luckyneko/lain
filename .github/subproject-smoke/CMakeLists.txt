# Consumes lain the way a downstream project would: add_subdirectory, then link one library.
#
# It is also lain's LIBRARY-ONLY build, for free. LAIN_BUILD_TESTING / _APPS / _FORMAT all
# default to ${LAIN_NOT_SUBPROJECT}, so being a subproject turns the tests, the app stack and
# the Vulkan loader off by itself — which is exactly the claim being checked: a headless
# consumer of lain::flow is not forced to vendor GLFW, ImGui or a driver.

cmake_minimum_required(VERSION 3.24)

project(lain-subproject-smoke LANGUAGES CXX)

if(NOT LAIN_SOURCE_DIR)
	message(FATAL_ERROR "Set LAIN_SOURCE_DIR to the lain source tree")
endif()

add_subdirectory("${LAIN_SOURCE_DIR}" lain)

add_executable(parent-smoke main.cpp)
target_link_libraries(parent-smoke PRIVATE lain::flow)
