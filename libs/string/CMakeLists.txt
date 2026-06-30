# lain::string — string utilities behind a lain:: face. Currently header-only:
# format() (a wrapper over fmt::format — C++17 has no std::format) plus a generic fmt
# formatter for any type exposing toString() (detected via a trait). The formatter
# lives here, not in lain::core, so a type becomes formattable just by having
# toString() and lain::core stays format-unaware. Depends only on fmt.

add_library(lain_string INTERFACE)
add_library(lain::string ALIAS lain_string)

target_compile_features(lain_string INTERFACE cxx_std_17)
target_include_directories(lain_string INTERFACE include)
target_link_libraries(lain_string INTERFACE fmt::fmt)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
