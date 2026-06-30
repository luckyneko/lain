# lain::meta — compile-time type/enum introspection behind a lain:: face. Header-only.
# enums.h: enum reflection over magic_enum (lain::meta::enums). typenames.h: typeName
# over nameof (lain::meta). Neither upstream is named past lain::meta. Constexpr type
# traits are intended to join this lib later.

add_library(lain_meta INTERFACE)
add_library(lain::meta ALIAS lain_meta)

target_compile_features(lain_meta INTERFACE cxx_std_17)
target_include_directories(lain_meta INTERFACE include)
target_link_libraries(lain_meta INTERFACE magic_enum::magic_enum nameof::nameof)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
