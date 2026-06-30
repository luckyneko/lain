# lain::meta — compile-time type/enum introspection behind a lain:: face. Header-only.
# Currently: enum reflection over magic_enum (names / values / name<->value casts);
# magic_enum is named nowhere past lain::meta. typeName + constexpr type traits are
# intended to join this lib later.

add_library(lain_meta INTERFACE)
add_library(lain::meta ALIAS lain_meta)

target_compile_features(lain_meta INTERFACE cxx_std_17)
target_include_directories(lain_meta INTERFACE include)
target_link_libraries(lain_meta INTERFACE magic_enum::magic_enum)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
