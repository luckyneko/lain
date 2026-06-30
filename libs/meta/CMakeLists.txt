# lain::meta — compile-time type/enum introspection behind a lain:: face. Header-only.
# enums.h: enum reflection over magic_enum (lain::meta::enums). typenames.h: typeName,
# an owned parse of the compiler's signature intrinsic (no external dep). traits.h:
# constexpr type traits (pure std). magic_enum is named nowhere past lain::meta::enums.

add_library(lain_meta INTERFACE)
add_library(lain::meta ALIAS lain_meta)

target_compile_features(lain_meta INTERFACE cxx_std_17)
target_include_directories(lain_meta INTERFACE include)
target_link_libraries(lain_meta INTERFACE magic_enum::magic_enum)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
