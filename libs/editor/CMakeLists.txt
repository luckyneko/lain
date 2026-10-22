# lain::editor — what every document editor in lain needs and no document type owns: an undo history
# of document snapshots, the recent-documents list and the session file it persists in, and the
# guard that asks before a destructive swap throws unsaved work away (WORK.md M15 slice 0). Hoisted
# out of flowview when a second editor (rigplan) needed the same rules, so the two cannot disagree
# about them. Driver-free and gui-free: an app draws its own question and decides what marks its
# document changed; the rules here are what its answers mean.

add_library(editor STATIC
	src/undo.cpp
	src/recents.cpp
	src/session.cpp
)
add_library(lain::editor ALIAS editor)

target_compile_features(editor PUBLIC cxx_std_17)
target_include_directories(editor PUBLIC include)
target_link_libraries(editor
	PUBLIC lain::data # a snapshot and a session are data::Values
	PRIVATE lain::core lain::io::data lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
