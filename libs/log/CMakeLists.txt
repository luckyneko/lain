# lain::log — a thin wrapper over spdlog. The public header exposes only lain's own
# Level enum + typed, fmt-formatted front-ends (info/warn/error/...); spdlog is named
# nowhere but log.cpp, so no spdlog:: leaks past lain::log. fmt (the shared external
# fmt spdlog is also built against) backs the compile-time-checked format strings the
# header needs — C++17 has no std::format.

add_library(lain_log STATIC
	src/log.cpp
)
add_library(lain::log ALIAS lain_log)

target_compile_features(lain_log PUBLIC cxx_std_17)
target_include_directories(lain_log PUBLIC include)

# fmt is PUBLIC: its format-string types appear in log.h, so consumers compile against
# it too. spdlog is PRIVATE: only log.cpp emits through it, keeping the backend hidden.
target_link_libraries(lain_log
	PUBLIC fmt::fmt
	PRIVATE spdlog::spdlog lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
