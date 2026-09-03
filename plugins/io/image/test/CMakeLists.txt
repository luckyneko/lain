# Cross-codec tests (Catch2): registerImageCodecs() wires the build-enabled codecs, and those
# codecs agree about ADR-0020's colour rule. The colour matrix lives here rather than in any one
# plugin because what it pins is the property the codecs SHARE — see the file's own note.

add_executable(test-io-image-codecs
	test_codecs.cpp
	test_colorroundtrip.cpp
)

target_compile_features(test-io-image-codecs PRIVATE cxx_std_17)
target_link_libraries(test-io-image-codecs PRIVATE lain::io::image::codecs lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image-codecs TEST_PREFIX "(io::image::codecs) ")
