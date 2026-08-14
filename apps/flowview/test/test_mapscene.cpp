// M8's end-to-end claim, driver-free: a folder of real image files on disk, listed IN-GRAPH, each
// processed by one shared subgraph, combined to a single result — through the production save/load
// facade and the production scheduler.
//
// This is the vertical the whole milestone was designed around (WORK.md M8, "first vertical"), and
// it is the only place every piece meets: ListDir's arity is unknowable until it runs, so the
// scheduler must stage; the map lifts its interior's pins through the port-type registry; the
// interface survives a round-trip carrying its split/broadcast choice; and Combine reads the
// gathered collection as an ordinary vector-valued input.

#include "graphio.h"

#include <lain/core/factory.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/combinenode.h>
#include <lain/flow/example/listdirnode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using lain::core::Factory;
using namespace lain::flow;
using lain::image::Image;
using lain::image::PixelFormat;

namespace
{
	Factory<Node> sceneFactory()
	{
		flowview::registerSceneSerialization(); // port types (incl. the collection forms) + the json codec
		lain::io::image::registerImageCodecs(); // png, so the fixtures below can be written and read
		Factory<Node> factory;
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		factory.registerType<MapNode>("map");
		factory.registerType<example::ListDirNode>("listDir");
		factory.registerType<example::LoadImageNode>("loadimage");
		factory.registerType<example::CombineNode>("combine");
		return factory;
	}

	std::filesystem::path scratchDir(const char* name)
	{
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / "flowview-mapscene-test" / name;
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	// A 4x4 solid-grey PNG. Solid and distinct per file, so the combined average is a number this
	// test can predict exactly rather than eyeball.
	void writeGrey(const std::filesystem::path& file, std::uint8_t level)
	{
		Image image(4, 4, PixelFormat::RGBA8);
		auto* px = image.data();
		for (std::size_t i = 0; i < image.pixelCount(); ++i)
		{
			px[i * 4 + 0] = level;
			px[i * 4 + 1] = level;
			px[i * 4 + 2] = level;
			px[i * 4 + 3] = 255;
		}
		REQUIRE(lain::io::image::save(file.string(), image));
	}

	Connection wire(Graph& g, NodeId from, PortId fromPin, NodeId to, std::size_t toIndex)
	{
		return g.connect(PortAddress{from, fromPin}, PortAddress{to, g.node(to).input(toIndex).id()});
	}

	Connection wire(Graph& g, NodeId from, std::size_t fromIndex, NodeId to, PortId toPin)
	{
		return g.connect(PortAddress{from, g.node(from).output(fromIndex).id()}, PortAddress{to, toPin});
	}

	// listDir(dir) -> map( file -> LoadImage ) -> combine -> result
	Graph buildMapScene(const std::filesystem::path& dir)
	{
		Graph graph;

		const NodeId listId = graph.add<example::ListDirNode>(dir.string());
		const NodeId mapId = graph.add<MapNode>();
		const NodeId combineId = graph.add<example::CombineNode>();

		// The map's interior: one file in, one image out. Written against ONE element — which is the
		// whole ergonomic point of a map, and why its face is lifted rather than its body.
		auto& map = static_cast<MapNode&>(graph.node(mapId));
		const PortId filePin = map.inner().boundaryInputNode().addBoundary<std::filesystem::path>("file");
		const PortId imagePin = map.inner().boundaryOutputNode().addBoundary<Image>("image");
		const NodeId loadId = map.inner().add<example::LoadImageNode>();
		REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), filePin, loadId, 0) == Connection::Ok);
		REQUIRE(wire(map.inner(), loadId, 0, map.inner().boundaryOutputNode().id(), imagePin) == Connection::Ok);

		// Derive the map's face from that interior, exactly as the host does — lifting path to
		// ListOfPath and Image to ListOfImage.
		edit::syncGroupPorts(graph, mapId);

		REQUIRE(graph.connect(listId, 0, mapId, 0) == Connection::Ok);
		REQUIRE(graph.connect(mapId, 0, combineId, 0) == Connection::Ok);
		const PortId resultPin = graph.boundaryOutputNode().addBoundary<Image>("result");
		REQUIRE(wire(graph, combineId, 0, graph.boundaryOutputNode().id(), resultPin) == Connection::Ok);
		return graph;
	}

	std::uint8_t resultLevel(const Graph& graph, const Evaluation& evaluation)
	{
		const std::vector<BoundaryPin> outputs = graph.boundaryOutputs();
		REQUIRE(outputs.size() == 1);
		const PortValue& value = evaluation.value(outputs.front());
		REQUIRE_FALSE(value.empty());
		const Image& image = value.get<Image>();
		REQUIRE(image.valid());
		return image.data()[0];
	}
} // namespace

TEST_CASE("a folder of images maps through one subgraph and combines", "[flowview][map]")
{
	const Factory<Node> factory = sceneFactory();
	const std::filesystem::path dir = scratchDir("folder");
	writeGrey(dir / "a.png", 30);
	writeGrey(dir / "b.png", 60);
	writeGrey(dir / "c.png", 90);

	Graph graph = buildMapScene(dir);
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	// Three files listed at runtime, three evaluations of one definition, averaged: (30+60+90)/3.
	const NodeId mapId = [&]
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (graph.node(id).evaluatesPerElement())
				return id;
		}
		return NodeId{};
	}();
	REQUIRE(evaluation.childCount(mapId) == 3);
	REQUIRE(resultLevel(graph, evaluation) == 60);

	SECTION("adding a file changes the arity on the next run, with no edit to the graph")
	{
		// The point of a data-dependent map: the RECIPE is untouched, and the run reshapes itself.
		writeGrey(dir / "d.png", 200);
		evaluation.requestRecomputeAll();
		SerialScheduler{}.run(graph, evaluation);

		REQUIRE(evaluation.childCount(mapId) == 4);
		REQUIRE(resultLevel(graph, evaluation) == 95); // (30+60+90+200)/4
	}

	SECTION("an empty folder maps to no image rather than a wrong one")
	{
		const std::filesystem::path empty = scratchDir("empty");
		for (const NodeId id : graph.nodeIds())
		{
			Node& node = graph.node(id);
			if (node.paramCount() > 0 && node.param(0).name() == "directory")
				REQUIRE(node.setParam<std::filesystem::path>(node.param(0).id(), empty));
		}
		SerialScheduler{}.run(graph, evaluation);

		REQUIRE(evaluation.childCount(mapId) == 0);
		// Combine has nothing to average, so it produces nothing and the boundary output is empty —
		// ADR-0007's ordinary suppression, with no map-specific machinery.
		REQUIRE(evaluation.value(graph.boundaryOutputs().front()).empty());
	}
}

TEST_CASE("a map scene survives save and load and runs the same", "[flowview][map]")
{
	// Through the PRODUCTION facade — the same saveGraph/loadGraph the menu bar calls — so this
	// covers the stored interface, the lifted port types naming themselves on disk, and the
	// re-derivation of the map's face from its rebuilt interior.
	const Factory<Node> factory = sceneFactory();
	const std::filesystem::path dir = scratchDir("roundtrip");
	// The images live in their OWN folder: listDir lists files, and a graph document saved beside
	// them is a file too — which the map would dutifully try to decode as an image.
	const std::filesystem::path images = dir / "images";
	std::filesystem::create_directories(images);
	writeGrey(images / "a.png", 40);
	writeGrey(images / "b.png", 80);

	const Graph built = buildMapScene(images);
	const std::filesystem::path document = dir / "scene.json";
	REQUIRE(flowview::saveGraph(document.string(), built, factory));

	// No template cache: this document links nothing, and "pass nullptr and mean it" is the
	// contract for a caller that genuinely has no host cache to own.
	serialize::LoadResult loaded = flowview::loadGraph(document.string(), factory, nullptr);
	REQUIRE(loaded.issues.empty());

	Evaluation evaluation{loaded.graph};
	SerialScheduler{}.run(loaded.graph, evaluation);
	REQUIRE(resultLevel(loaded.graph, evaluation) == 60); // (40+80)/2

	SECTION("and re-saving is byte-identical")
	{
		const std::filesystem::path again = dir / "again.json";
		REQUIRE(flowview::saveGraph(again.string(), loaded.graph, factory));
		REQUIRE(flowview::loadGraph(again.string(), factory, nullptr).issues.empty());

		// Compare the documents themselves, not the graphs: a round-trip that quietly rewrites a
		// user's file is the failure this guards.
		const auto readAll = [](const std::filesystem::path& p)
		{
			std::ifstream in(p, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
		REQUIRE(readAll(document) == readAll(again));
	}
}
