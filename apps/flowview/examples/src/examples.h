#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <filesystem>
#include <string>
#include <vector>

// The example documents in apps/flowview/examples: ONE table, read by the tool that writes them
// (flowview-examples) and by the test that holds them to it (test_examples.cpp). A second list of
// what the examples are is how the two would come to disagree without anything saying so.
namespace flowview::examples
{
	struct Context;
	struct Document;

	struct Example
	{
		std::string name;		   // the file stem: <name>.json
		std::string summary;	   // one line: what it shows
		bool needsVideo = false;   // reads data/shot.mp4, so it runs only with a video codec plugin
		std::string expectedIssue; // empty: loads and runs clean; else a load or Issues-pane row it must raise
		// How to build it, or null for a FROZEN file written by hand. broken-v1.json is the only one:
		// nothing can write a version-1 document any more, which is the point of keeping one.
		Document (*build)(const Context&) = nullptr;
		// Names camera node kinds, which run only with a camera backend. Without one the document still
		// loads whole (a kind is vocabulary, ADR-0016 amended) and its nodes report the missing backend
		// when run, exactly as needsVideo's do. Last, so the entries that do not need it need not say so.
		bool needsCamera = false;
	};

	// Every example, in the order they are written. A linked template precedes the documents that
	// link it, because a link is resolved (read back from disk) as it is built.
	const std::vector<Example>& catalog();

	// Write every built document into `folder`, the examples folder. Media paths name the data sets
	// under `folder`/data, and are stored relative to the document. Node ids are renumbered from a
	// counter in the order they first appear, so an unchanged example is written byte for byte as it
	// was. Throws std::runtime_error naming the example on any failure.
	void writeDocuments(const std::filesystem::path& folder, const lain::core::Factory<lain::flow::Node>& factory);

	// Write the data sets the media examples read, under `folder`/data: stills/, shot/, holes/, and
	// shot.mp4 when the build has a video writer (an existing one is otherwise left alone). Throws
	// std::runtime_error on any failure.
	void writeData(const std::filesystem::path& folder);
} // namespace flowview::examples
