#pragma once

#include <lain/rigplan/rigplan.h>

#include <filesystem>
#include <string>
#include <vector>

// The example rig plans in apps/rigplan/examples: ONE table, read by the tool that writes them
// (rigplan-examples) and by the test that holds them to it (apps/rigplan/test). A second list of what
// the examples are is how the two would come to disagree without anything saying so.
//
// Committing the first of them froze the rig plan document at version 1 (M15 slice 7): from here a
// change to the document's shape is a new version and a migrator, and the byte comparison of these
// files against the builder is what notices one.
namespace rigplan::examples
{
	struct Example
	{
		std::string name;	 // the file stem: <name>.json
		std::string summary; // one line: what it shows
		lain::rigplan::RigPlan (*build)() = nullptr;
		// Waves a board, so a capture of it needs a board renderer (a camera build). Without one the plan
		// still reads and saves whole, and a capture is refused naming the board.
		bool needsBoardRenderer = false;
	};

	// Every example, in the order they are written.
	const std::vector<Example>& catalog();

	// Write every example into `folder`, the examples folder, as rig plan documents with their paths
	// relative to it (ADR-0027). Identities are written as the builders state them, so an unchanged
	// example is written byte for byte as it was. Needs the json writer registered. Throws
	// std::runtime_error naming the example on any failure.
	void writeDocuments(const std::filesystem::path& folder);
} // namespace rigplan::examples
