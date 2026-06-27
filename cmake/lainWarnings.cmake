# Shared strict-warning flags for lain's own targets.
#
# The siblings inline this genex per target (see multi's CMakeLists); the umbrella
# owns several libs + apps, so we hand it out once as an INTERFACE target. Link it
# PRIVATE on every lain-authored target — never on a vendored/third-party one,
# whose headers we re-expose as SYSTEM instead.

if(NOT TARGET lain_warnings)
	add_library(lain_warnings INTERFACE)
	add_library(lain::warnings ALIAS lain_warnings)

	target_compile_options(lain_warnings INTERFACE
		$<$<CXX_COMPILER_ID:MSVC>:/WX;/W4>
		$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror;-Wall;-Wextra>
	)
endif()
