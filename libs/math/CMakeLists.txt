# lain::math — typed names over GLM (header-only). A generic Vec<N,T>/Mat<C,R,T>
# core, per-dimension Vec2/3/4, and named concretes; GLM's free functions are
# re-exposed as lain::math. Nothing of GLM leaks past lain::math beyond the glm::
# types the aliases name.

add_library(lain_math INTERFACE)
add_library(lain::math ALIAS lain_math)

target_compile_features(lain_math INTERFACE cxx_std_17)
target_include_directories(lain_math INTERFACE include)
target_link_libraries(lain_math INTERFACE glm::glm)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
