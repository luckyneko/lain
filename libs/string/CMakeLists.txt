# lain::string — string utilities behind a lain:: face. format() (a wrapper over fmt::format —
# C++17 has no std::format) plus a generic fmt formatter for any type exposing toString()
# (detected via lain::meta::has_to_string), and the <key> pattern system (pattern.h). The
# formatter lives here, not in lain::core, so a type becomes formattable just by having
# toString() and lain::core stays format-unaware. Depends on fmt + lain::meta.
#
# It is a compiled library rather than INTERFACE because of pattern.cpp. format.h stays
# header-only and pattern.h is a SEPARATE header: lain::log includes format.h at every log site
# in the tree, and must not start carrying a parser with it.
#
# lain::log links THIS target PUBLICly, to carry that formatter to every log site in the tree. So
# the dependency runs one way only: this target must never link lain::log, and a diagnostic needed
# here goes to the caller instead — which is why Pattern reports a refusal by return type.

add_library(lain_string STATIC
	src/pattern.cpp
)
add_library(lain::string ALIAS lain_string)

target_compile_features(lain_string PUBLIC cxx_std_17)
target_include_directories(lain_string PUBLIC include)
target_link_libraries(lain_string
	PUBLIC fmt::fmt lain::meta
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
