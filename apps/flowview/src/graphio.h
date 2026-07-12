#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/flow/serialize/loadresult.h>
#include <lain/flow/serialize/valuecodecs.h>

#include <string>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// flowview's serialization wiring: the value-serializer registry for its param types, the
	// one-time registrations, and the save/load facade over flow::serialize + lain::io::data. Kept
	// here (not in flow::serialize) because it names the concrete param/port types the app uses.

	// The value-serializer registry for flowview's param types: int / float (blur), path
	// (loadimage), image::ColorRGBf (tint). The app-populated ValueCodecs the graph walk needs.
	lain::flow::serialize::ValueCodecs sceneCodecs();

	// One-time registration serialization needs: the image::Image port type (so boundary pins
	// replay) and the data codecs (json). Idempotent — safe to call in either mode's startup.
	void registerSceneSerialization();

	// Save `graph` as JSON at `uri` (io::data picks the codec by extension). `editor` is the
	// adapter's per-node metadata (canvas positions), keyed by NodeId. Returns false on a write /
	// encode failure (logged).
	bool saveGraph(const std::string& uri, const lain::flow::Graph& graph,
				   const lain::core::Factory<lain::flow::Node>& factory,
				   const lain::flow::serialize::EditorData& editor = {});

	// Load a graph from JSON at `uri`. Best-effort: an unreadable file is a fatal Error in the
	// returned LoadResult (which then carries an empty graph).
	lain::flow::serialize::LoadResult loadGraph(const std::string& uri,
												const lain::core::Factory<lain::flow::Node>& factory);
} // namespace flowview
