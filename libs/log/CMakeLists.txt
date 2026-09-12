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
#
# lain::string is PUBLIC because log.h includes its toString() formatter, which has to be
# visible in the consumer's TU for "{}" to render a lain type. What log.h pulls in is that
# header alone — lain::string's <key> pattern system is a separate header, so the weight here
# stays a formatter plus a trait header. THE DEPENDENCY RUNS ONE WAY: lain::string must never
# link lain::log, or this becomes a cycle.
#
# Forward reference, and that is fine: libs/string is added after libs/log in the root
# CMakeLists, but a name containing "::" is resolved at GENERATE time — libs/string
# already forward-references lain::meta the same way.
target_link_libraries(lain_log
	PUBLIC fmt::fmt lain::string
	PRIVATE spdlog::spdlog lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
