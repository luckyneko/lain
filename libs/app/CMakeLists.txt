# lain::app — a reusable application framework over GLFW + CLI11. An Application owns
# the Vulkan instance, one shared device, the windows it creates, and the run loop;
# behaviour comes from an ApplicationDelegate (+ a WindowDelegate per window).
# archimedes (acm::), lain::core (Time/TimeState), lain::math (InputState vectors) and
# CLI11 (re-exposed as lain::app::cli) are public; GLFW is private (no glfw symbol
# leaks past lain::app).

# The third-party notice text --licenses prints, assembled from whatever dependencies this
# build actually contains. Generated here rather than checked in, so it cannot go stale
# about a version or claim a dependency that was not built (cmake/lainNotices.cmake).
include(lainNotices)
lain_generate_notices(LAIN_NOTICES_SOURCE)

add_library(app STATIC
	src/application.cpp
	src/window.cpp
	"${LAIN_NOTICES_SOURCE}"
)
add_library(lain::app ALIAS app)

target_compile_features(app PUBLIC cxx_std_17)
target_include_directories(app PUBLIC include PRIVATE src)

target_link_libraries(app
	PUBLIC archimedes lain::core lain::math CLI11::CLI11
	PRIVATE glfw lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
