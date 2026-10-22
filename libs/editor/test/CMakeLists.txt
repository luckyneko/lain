# lain::editor tests (Catch2). Pure unit tests — no driver, no window; the session cases write only to
# scratch paths.

add_executable(test-editor
	test_undo.cpp
	test_recents.cpp
	test_session.cpp
	test_guard.cpp
)

target_compile_features(test-editor PRIVATE cxx_std_17)
target_link_libraries(test-editor PRIVATE lain::editor lain::io::data::codecs lain::testing lain::warnings Catch2::Catch2WithMain)
# The session cases that read and write a file need the json codec, an optional plugin. Asked of the
# plugin target at generate time: the plugins configure after libs/, so it does not exist yet here.
target_compile_definitions(test-editor PRIVATE LAIN_EDITOR_TEST_JSON=$<BOOL:$<TARGET_EXISTS:lain::io::data::json>>)

catch_discover_tests(test-editor TEST_PREFIX "(editor) ")
