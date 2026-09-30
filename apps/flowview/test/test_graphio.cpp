// flowview's save/load facade. Driver-free — no window, no device — but it does touch the FILE
// SYSTEM, because that is the whole point of the case below: a linked group's template is a separate
// document, and resolving it is a file read.
//
// The bug this exists for: restoreGraph (the undo/redo path) took no TemplateResolver, so undoing in
// a document containing a linked group rebuilt that group as an UNRESOLVED placeholder — its interior
// emptied and it stopped producing output. Nothing warned, because loading unresolved is a legitimate
// mode (a headless `flowview list` wants exactly that).

#include "graphio.h"
#include "scene.h" // registerExampleNodes: the palette, for the nodes that carry a path

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>
#include <utility>

using lain::core::Factory;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// Only what the documents below name — the boundary pair and a linked group. No example nodes,
	// so this test needs neither a device nor an image codec.
	Factory<Node> ioFactory()
	{
		flowview::registerSceneSerialization(); // the Image port type + the json codec
		Factory<Node> factory;
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		factory.registerType<LinkedGroupNode>("linkedGroup");
		return factory;
	}

	// A scratch directory of this test's own, removed and recreated per case so a rerun is clean.
	std::filesystem::path scratchDir(const char* name)
	{
		const std::filesystem::path dir = lain::testing::scratchDir() / "graphio" / name;
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	// A template document with `inputs` input pins and one output pin, written to `path`.
	void writeTemplate(const Factory<Node>& factory, const std::filesystem::path& path, int inputs = 1)
	{
		Graph templateGraph;
		for (int i = 0; i < inputs; ++i)
			templateGraph.boundaryInputNode().addBoundary<lain::image::Image>("source" + std::to_string(i));
		templateGraph.boundaryOutputNode().addBoundary<lain::image::Image>("result");
		REQUIRE(flowview::saveGraph(path.string(), templateGraph, factory));
	}

	// A parent graph holding one linked group whose `source` is `source`, with the group's ports
	// already mirrored from the resolved template.
	Graph parentLinking(const std::string& source, const Factory<Node>& factory, const std::filesystem::path& dir)
	{
		Graph parent;
		const NodeId id = parent.add<LinkedGroupNode>();
		static_cast<LinkedGroupNode&>(parent.node(id)).setSource(source);

		// Resolve it once through the normal load path, so the fixture starts from the same state the
		// app is in when the user hits undo: a RESOLVED linked group.
		const lain::data::Value document = flowview::snapshotGraph(parent, factory);
		LoadResult loaded = flowview::restoreGraph(document, factory, dir, nullptr);
		REQUIRE(loaded.clean());
		return std::move(loaded.graph);
	}

	const LinkedGroupNode* onlyLink(const Graph& graph)
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (const auto* linked = dynamic_cast<const LinkedGroupNode*>(&graph.node(id)))
				return linked;
		}
		return nullptr;
	}
} // namespace

TEST_CASE("restoreGraph resolves a linked group's template", "[graphio]")
{
	// The undo/redo path. A snapshot stores a linked group the same way the file does — source plus
	// interface cache — so a restore has to follow the link, or the group comes back empty.
	const std::filesystem::path dir = scratchDir("resolve");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	Graph parent = parentLinking("template.json", factory, dir);
	REQUIRE(onlyLink(parent) != nullptr);
	REQUIRE(onlyLink(parent)->resolved()); // the fixture starts resolved, as the app does

	// Snapshot and restore, exactly as Undo does.
	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, dir, nullptr);
	REQUIRE(restored.clean());

	const LinkedGroupNode* link = onlyLink(restored.graph);
	REQUIRE(link != nullptr);
	REQUIRE(link->resolved()); // ... and comes back resolved, not as an empty placeholder
	REQUIRE(link->innerGraph()->boundaryInputNode().outputCount() == 1);
	REQUIRE(link->innerGraph()->boundaryOutputNode().inputCount() == 1);
	REQUIRE(link->inputCount() == 1); // the group's own mirrored ports survive with it
	REQUIRE(link->outputCount() == 1);
}

TEST_CASE("restoreGraph resolves a template named by an absolute path", "[graphio]")
{
	// A `source` typed or dropped in as an absolute path is unaffected by the document folder —
	// worth pinning down, since operator/ discards the left side for an absolute right side.
	const std::filesystem::path dir = scratchDir("absolute");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path templatePath = dir / "template.json";
	writeTemplate(factory, templatePath);

	Graph parent = parentLinking(templatePath.string(), factory, {});

	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, {}, nullptr);
	REQUIRE(onlyLink(restored.graph) != nullptr);
	REQUIRE(onlyLink(restored.graph)->resolved());
}

TEST_CASE("a template that cannot be found still restores as a repairable placeholder", "[graphio]")
{
	// The other half of the contract: an unresolved link is not a load failure. Its cached interface
	// rebuilds the pins, so the parent's wiring survives and re-saving is lossless — that is what
	// makes a broken link repairable rather than destructive.
	const std::filesystem::path dir = scratchDir("missing");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value snapshot = flowview::snapshotGraph(parent, factory);

	std::error_code ec;
	std::filesystem::remove(dir / "template.json", ec); // the template goes away under us

	// No cache: with one, the deleted template would still resolve from the entry the first load
	// stored, which is the documented cost of having no file watching.
	LoadResult restored = flowview::restoreGraph(snapshot, factory, dir, nullptr);
	REQUIRE_FALSE(restored.clean()); // reported...
	const LinkedGroupNode* link = onlyLink(restored.graph);
	REQUIRE(link != nullptr);
	REQUIRE_FALSE(link->resolved());
	REQUIRE(link->inputCount() == 1); // ... but the face is rebuilt from the cache, so edges still land
	REQUIRE(link->outputCount() == 1);
}

TEST_CASE("an undo restore keeps the template definition it already had", "[graphio]")
{
	// Why the host's cache survives undo/redo while it is cleared on New/Open: a restore rebuilds the
	// document, so without the cache the template would be rebuilt too and its inner nodes would come
	// back with... the same ids (a load preserves identity), but as a DIFFERENT definition — and every
	// instance would drift apart again. Keeping it is what makes an undo a no-op for a template.
	const std::filesystem::path dir = scratchDir("undo-cache");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	lain::flow::serialize::TemplateCache cache;
	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value snapshot = flowview::snapshotGraph(parent, factory);

	LoadResult first = flowview::restoreGraph(snapshot, factory, dir, &cache);
	REQUIRE(first.clean());
	REQUIRE(cache.size() == 1);
	const std::shared_ptr<const Graph> definition = onlyLink(first.graph)->definition();

	LoadResult second = flowview::restoreGraph(snapshot, factory, dir, &cache); // undo, then redo
	REQUIRE(second.clean());
	REQUIRE(onlyLink(second.graph)->definition() == definition);
}

// --- the template cache's keys and its invalidation (M7 slice 3) -------------

TEST_CASE("one file has one template key, however it is spelled", "[graphio]")
{
	// The cache is keyed on this, and so is the cycle guard. A key computed two ways is a key that
	// eventually disagrees with itself — and the failure is SILENT: an invalidation that misses just
	// keeps serving the definition it was asked to drop.
	const std::filesystem::path dir = scratchDir("keys");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	const lain::core::Uri direct = flowview::templateKey(dir / "template.json");
	REQUIRE(flowview::templateKey(dir / "sub" / ".." / "template.json") == direct);
	REQUIRE(flowview::templateKey(dir / "./template.json") == direct);

	// ... and it is the key the RESOLVER reports, which is what makes save-invalidation land on the
	// entry a load created.
	const auto resolved = flowview::templateResolver(dir)("./template.json");
	REQUIRE(resolved.has_value());
	REQUIRE(resolved->key == direct);
}

TEST_CASE("invalidating a template's key is what makes an edit to it visible", "[graphio]")
{
	// The mechanism behind BOTH of slice 3's triggers: Reload Linked Groups (clear the cache) and
	// saving a document (drop that path's entry, so Edit Template... -> Save -> Return shows the edit).
	// Without the drop the cache keeps serving the pre-edit definition — which is the behaviour being
	// pinned here, not merely the fix.
	const std::filesystem::path dir = scratchDir("invalidate");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path templatePath = dir / "template.json";
	writeTemplate(factory, templatePath, 1);

	lain::flow::serialize::TemplateCache cache;
	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value document = flowview::snapshotGraph(parent, factory);
	REQUIRE(flowview::restoreGraph(document, factory, dir, &cache).clean());

	// The template gains a pin, out from under the running app.
	writeTemplate(factory, templatePath, 2);

	SECTION("a stale entry keeps serving the old definition")
	{
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		REQUIRE(onlyLink(again.graph)->innerGraph()->boundaryInputNode().outputCount() == 1);
	}

	SECTION("dropping that one key picks the edit up")
	{
		cache.invalidate(flowview::templateKey(templatePath).toString());
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		const LinkedGroupNode* link = onlyLink(again.graph);
		REQUIRE(link->innerGraph()->boundaryInputNode().outputCount() == 2);
		REQUIRE(link->inputCount() == 2); // ... and the group's own face followed it
	}

	SECTION("clearing the whole cache does too — the reload gesture")
	{
		cache.clear();
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		REQUIRE(onlyLink(again.graph)->innerGraph()->boundaryInputNode().outputCount() == 2);
	}
}

// --- document options (M14 slice 5) -------------------------------------------
//
// A document's run trigger is saved with it (the root editor blob) but is not an undo step — so it is
// written by saveGraph, stripped from a load by takeDocumentOptions, and absent from every snapshot.

// A document with one image input on its boundary, so a round-trip has something to carry besides the
// options.
static Graph optionsDocument()
{
	Graph graph;
	graph.boundaryInputNode().addBoundary<lain::image::Image>("source");
	return graph;
}

TEST_CASE("a document's run trigger is saved with it and read back", "[graphio][options]")
{
	const std::filesystem::path dir = scratchDir("options-roundtrip");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	for (const flowview::RunTrigger trigger : {flowview::RunTrigger::OnCommit, flowview::RunTrigger::Manual})
	{
		flowview::DocumentOptions options;
		options.trigger = trigger;
		REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, {}, options));

		LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
		REQUIRE(loaded.clean());
		REQUIRE(flowview::takeDocumentOptions(loaded) == options);
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1); // and the document is all there
	}
}

TEST_CASE("a document at the default options is written without them", "[graphio][options]")
{
	// Absent means Live, so a Live document writes no blob at all — which is what keeps every file
	// saved before options existed byte-identical when it is saved again.
	const std::filesystem::path dir = scratchDir("options-default");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	// And the options OWN the root blob: one left in the layout does not leak through.
	EditorTree layout;
	lain::data::Value stale = lain::data::Value::object();
	stale.set("trigger", lain::data::Value(std::string("Manual")));
	layout.graph = stale;

	REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, layout, flowview::DocumentOptions{}));
	const auto written = lain::io::data::load(lain::core::Uri::fromPath(file));
	REQUIRE(written.has_value());
	REQUIRE(written->find("graphEditor") == nullptr);

	LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
	REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
}

TEST_CASE("options this build does not understand fall back to Live and the document still loads", "[graphio][options]")
{
	// The graph must never be the price of a blob this build cannot read.
	const std::filesystem::path dir = scratchDir("options-unknown");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	const auto loadWith = [&](lain::data::Value blob)
	{
		lain::data::Value document = flowview::snapshotGraph(optionsDocument(), factory);
		document.set("graphEditor", std::move(blob));
		REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(file), document));
		LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
		REQUIRE(loaded.clean()); // flow round-trips the blob opaquely — it is not flow's to judge
		return loaded;
	};

	SECTION("a trigger a newer flowview wrote")
	{
		// lain::data reads a struct member best-effort, so this falls back to Live on its own.
		lain::data::Value blob = lain::data::Value::object();
		blob.set("trigger", lain::data::Value(std::string("WhenIdle")));
		LoadResult loaded = loadWith(std::move(blob));
		REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1);
	}

	SECTION("a blob that is not options at all")
	{
		LoadResult loaded = loadWith(lain::data::Value(std::string("manual please")));
		REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1);
		// ... and the user is told, since a Manual document quietly running Live is what the option
		// exists to prevent.
		REQUIRE(loaded.issues.size() == 1);
		REQUIRE(loaded.issues.front().severity == Severity::Warning);
	}
}

TEST_CASE("a loaded document's snapshots never carry its options", "[graphio][options]")
{
	// The undo half of "saved, never undone". The layout a load leaves behind is the undo baseline and
	// every snapshot after it — so if the options stayed in it, the whole history would carry a frozen
	// copy of the trigger, one line away from being reapplied by a restore.
	const std::filesystem::path dir = scratchDir("options-snapshot");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	flowview::DocumentOptions manual;
	manual.trigger = flowview::RunTrigger::Manual;
	REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, {}, manual));

	LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
	REQUIRE(flowview::takeDocumentOptions(loaded) == manual);
	REQUIRE(loaded.editor.graph.isNull());

	const lain::data::Value baseline = flowview::snapshotGraph(loaded.graph, factory, loaded.editor);
	REQUIRE(baseline.find("graphEditor") == nullptr);
	// ... and so an undo restore of that snapshot hands back no options to apply.
	LoadResult restored = flowview::restoreGraph(baseline, factory, dir, nullptr);
	REQUIRE(restored.editor.graph.isNull());
}

// --- document-relative paths at the file boundary (ADR-0027) -----------------
//
// The transforms are flow::serialize's and are pinned there (test_documentpaths.cpp). These cases
// are what only the HOST can get wrong: applying them at every file it reads or writes, against the
// right folder, and at nothing else.

static Factory<Node> mediaFactory()
{
	flowview::registerSceneSerialization();
	Factory<Node> factory;
	flowview::registerExampleNodes(factory, 8);
	return factory;
}

// The in-memory path of the first LoadImage in `graph` (not descending).
static std::filesystem::path loadPath(const Graph& graph)
{
	for (const NodeId id : graph.nodeIds())
	{
		const Node& node = graph.node(id);
		if (dynamic_cast<const lain::flow::example::LoadImageNode*>(&node) == nullptr)
			continue;
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == "path")
				return node.param(i).get<std::filesystem::path>();
		}
	}
	FAIL("no loadimage path");
	return {};
}

// What `file` stores as the first LoadImage's path.
static std::string storedLoadPath(const std::filesystem::path& file)
{
	const auto document = lain::io::data::load(lain::core::Uri::fromPath(file));
	REQUIRE(document.has_value());
	for (const lain::data::Value& node : *document->find("nodes")->asArray())
	{
		if (*node.find("kind")->asString() != "loadimage")
			continue;
		for (const lain::data::Value& param : *node.find("params")->asArray())
		{
			if (*param.find("name")->asString() == "path")
				return *param.find("value")->asString();
		}
	}
	FAIL("no loadimage path stored");
	return {};
}

static Graph loadingDocument(const std::filesystem::path& path)
{
	Graph graph;
	graph.add<lain::flow::example::LoadImageNode>(path.string());
	return graph;
}

TEST_CASE("a project folder saved and moved whole still finds its media", "[graphio][mediapath]")
{
	const std::filesystem::path from = scratchDir("media-move-from");
	const std::filesystem::path to = scratchDir("media-move-to");
	const Factory<Node> factory = mediaFactory();

	REQUIRE(flowview::saveGraph(lain::core::Uri::fromPath(from / "doc.json"), loadingDocument(from / "data" / "a.png"),
								factory));
	REQUIRE(storedLoadPath(from / "doc.json") == "data/a.png");

	// Before the rule, a document named its footage by the absolute path it was saved with, so this
	// copy would have read the ORIGINAL folder's file, or nothing.
	std::filesystem::copy_file(from / "doc.json", to / "doc.json");
	LoadResult loaded = flowview::loadGraph(lain::core::Uri::fromPath(to / "doc.json"), factory, nullptr);
	REQUIRE(loaded.clean());
	REQUIRE(loadPath(loaded.graph) == (to / "data" / "a.png").lexically_normal());
}

TEST_CASE("an undo snapshot keeps media paths exactly as held", "[graphio][mediapath]")
{
	// A snapshot never touches a file, so nothing rebases it. A relative path there is the working
	// directory's, and a restore that rebased it would re-point an undo at a different file.
	const std::filesystem::path dir = scratchDir("media-snapshot");
	const Factory<Node> factory = mediaFactory();
	const std::filesystem::path path = GENERATE(std::filesystem::path{"rel.png"}, std::filesystem::path{"/abs/a.png"});

	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(loadingDocument(path), factory), factory, dir, nullptr);
	REQUIRE(loadPath(restored.graph) == path);
}

TEST_CASE("a linked template's media are relative to the template file", "[graphio][mediapath]")
{
	// A template is a document saved beside its own data. Read against the PARENT's folder, a template
	// shared by two projects would find its media in one of them at most.
	const std::filesystem::path root = scratchDir("media-template");
	const Factory<Node> factory = mediaFactory();
	std::filesystem::create_directories(root / "lib");

	Graph templateGraph = loadingDocument(root / "lib" / "a.png");
	templateGraph.boundaryOutputNode().addBoundary<lain::image::Image>("result");
	REQUIRE(flowview::saveGraph(lain::core::Uri::fromPath(root / "lib" / "template.json"), templateGraph, factory));
	REQUIRE(storedLoadPath(root / "lib" / "template.json") == "a.png");

	Graph parent;
	static_cast<LinkedGroupNode&>(parent.node(parent.add<LinkedGroupNode>())).setSource("lib/template.json");
	REQUIRE(flowview::saveGraph(lain::core::Uri::fromPath(root / "doc.json"), parent, factory));

	LoadResult loaded = flowview::loadGraph(lain::core::Uri::fromPath(root / "doc.json"), factory, nullptr);
	const LinkedGroupNode* link = onlyLink(loaded.graph);
	REQUIRE(link != nullptr);
	REQUIRE(link->resolved());
	REQUIRE(loadPath(*link->innerGraph()) == (root / "lib" / "a.png").lexically_normal());
}

// --- a gradient records its size ------------------------------------------------

// A gradient's width and height defaults, as the node holds them.
static std::pair<int, int> gradientSize(const Graph& graph)
{
	for (const NodeId id : graph.nodeIds())
	{
		const Node& node = graph.node(id);
		if (dynamic_cast<const lain::flow::example::GradientNode*>(&node) == nullptr)
			continue;
		return {node.defaultOf(node.input(0).id())->get<int>(), node.defaultOf(node.input(1).id())->get<int>()};
	}
	FAIL("no gradient");
	return {};
}

TEST_CASE("a gradient's size travels with its document, not with --size", "[graphio][gradient]")
{
	// Its size used to be a construction value the factory took from --size, so one document
	// rendered 64 px in one session and 512 px in another.
	const std::filesystem::path dir = scratchDir("gradient-size");
	flowview::registerSceneSerialization();
	Factory<Node> small;
	flowview::registerExampleNodes(small, 8);
	Factory<Node> large;
	flowview::registerExampleNodes(large, 64);

	Graph graph;
	const NodeId id = graph.add(small.create("gradient"));
	Node& gradient = graph.node(id);
	REQUIRE(gradient.setParam(gradient.defaultOf(gradient.input(0).id())->id(), 16));
	REQUIRE(flowview::saveGraph(lain::core::Uri::fromPath(dir / "doc.json"), graph, small));

	SECTION("a session with another --size loads the saved size")
	{
		LoadResult loaded = flowview::loadGraph(lain::core::Uri::fromPath(dir / "doc.json"), large, nullptr);
		REQUIRE(loaded.clean());
		REQUIRE(gradientSize(loaded.graph) == std::pair<int, int>{16, 8});
	}

	SECTION("a document saved before the size was stored keeps behaving as it did")
	{
		// Strip the params, as every older file lacks them: the loading session's preset stands.
		auto document = lain::io::data::load(lain::core::Uri::fromPath(dir / "doc.json"));
		REQUIRE(document.has_value());
		for (auto& [key, value] : *document->asObject())
		{
			if (key != "nodes")
				continue;
			for (lain::data::Value& node : *value.asArray())
			{
				if (*node.find("kind")->asString() == "gradient")
					node.set("params", lain::data::Value::array());
			}
		}
		REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(dir / "old.json"), *document));
		LoadResult loaded = flowview::loadGraph(lain::core::Uri::fromPath(dir / "old.json"), large, nullptr);
		REQUIRE(loaded.clean());
		REQUIRE(gradientSize(loaded.graph) == std::pair<int, int>{64, 64});
	}
}
