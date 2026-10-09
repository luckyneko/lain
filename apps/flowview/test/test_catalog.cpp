// The node catalog — what the gui's three add surfaces offer (the menu bar's Add, the canvas
// right-click palette, and the Nodes pane all read nodeCatalog()).
//
// M10 slice 7 is where the frame-sequence kinds joined it: until then they were registered in the
// factory alone, reachable from the cli and from a saved document but from no menu. The invariant
// worth pinning is the one a hand-added entry breaks silently — a key the factory cannot build is a
// menu item that does nothing at all, which is M5's bug six (a gesture that compiled, linked, and
// was unreachable) arriving from the other direction.

#include "graphio.h" // sceneCodecs — what a document can carry a param as
#include "scene.h"

#include <lain/camera/backends.h>
#include <lain/camera/flow/register.h>
#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace lain;

namespace
{
	std::vector<std::string> catalogKeys()
	{
		std::vector<std::string> keys;
		for (const flowview::NodeCategory& category : flowview::nodeCatalog())
			keys.insert(keys.end(), category.keys.begin(), category.keys.end());
		return keys;
	}

	bool offers(const std::string& key)
	{
		const std::vector<std::string> keys = catalogKeys();
		return std::find(keys.begin(), keys.end(), key) != keys.end();
	}
} // namespace

TEST_CASE("every catalog kind can actually be created", "[catalog]")
{
	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);

	const std::vector<std::string> keys = catalogKeys();
	REQUIRE_FALSE(keys.empty());
	for (const std::string& key : keys)
	{
		INFO("catalog key: " << key);
		CHECK(factory.create(key) != nullptr);
	}
}

TEST_CASE("the frame-sequence kinds are on the menu", "[catalog]")
{
	// The three that make footage reachable without hand-editing a document: bring it in, take one
	// frame of it, trim the range.
	CHECK(offers("openSequence"));
	CHECK(offers("frameAt"));
	CHECK(offers("clipSequence"));

	// The LOOP (M11). Until slice 6 nothing in the tree constructed one — it ran, folded and
	// persisted, and was reachable from no menu and no document a user could write. The first case
	// above is what makes this line mean something: the key is offered AND the factory builds it.
	CHECK(offers("loop"));
	// ... with the two example nodes that make its condition path real. Without them the reserved
	// `continue` pin would be built and never driven by anything.
	CHECK(offers("imageDifference"));
	CHECK(offers("compare"));

	// The boundary pair stays OFF it — one each per graph, born with the graph and grown from the
	// Interface panel, not added like an ordinary node.
	CHECK_FALSE(offers("groupInput"));
	CHECK_FALSE(offers("groupOutput"));
	// ... and so does a linked group, which needs a template chosen first (the menu bar adds it
	// through a file dialog).
	CHECK_FALSE(offers("linkedGroup"));
}

TEST_CASE("the camera kinds always load, and are on the menu exactly when a backend can run them", "[catalog]")
{
	// ADR-0016, amended: a camera kind is vocabulary, so the factory has it in every build and a
	// camera document loads whole; the menu offers it only where it can function. Asked of the
	// production path, backends and all, and held to the build's configuration BOTH ways: a build
	// with the backends a kind needs that does not offer it is as wrong as one without them that does.
	//
	// What each kind needs is written HERE, per kind, rather than read from the library, so the
	// library's rule is held to the configuration and not to itself. The board kinds run on OpenCV's
	// backends; both registrations also refine the rig, which is Ceres'. Every kind the library has
	// is asked about, so a kind added there without an expectation here fails rather than going
	// unchecked. The single OpenCV flag this replaced could not say what registerCameras needs, so
	// that kind was left off the list it kept, and nothing noticed.
	camera::registerCameraBackends();
	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);

	const bool opencv = LAIN_EXPECT_CAMERA_BACKEND != 0;
	const bool refiner = LAIN_EXPECT_CAMERA_REFINER != 0;
	const std::map<std::string, bool> expected{
		{camera::kBoardSpecificationKey, opencv},
		{camera::kRenderBoardKey, opencv},
		{camera::kDetectBoardKey, opencv},
		{camera::kCalibrateCameraKey, opencv},
		{camera::kCameraModelKey, opencv},
		{camera::kRegisterCamerasKey, opencv && refiner},
		{camera::kRegisterCamerasTargetlessKey, opencv && refiner},
	};
	const std::vector<std::string> kinds = camera::cameraNodeKeys();
	CHECK(kinds.size() == expected.size());
	for (const std::string& key : kinds)
	{
		INFO("camera kind: " << key);
		const auto found = expected.find(key);
		CHECK(found != expected.end()); // a camera kind with no expectation here
		if (found != expected.end())
			CHECK(offers(key) == found->second);
		CHECK(factory.create(key) != nullptr);
	}
}

TEST_CASE("every parameter of every kind can be saved", "[catalog]")
{
	// flow::serialize writes a param through the codec registered for its type and SKIPS one with
	// none, silently: the document then loads with the default in its place. So a kind whose param
	// type was never given a codec loses that setting on every save, and nothing says so. Asked of
	// every kind the production factory has, which is every camera kind in every build, backends or
	// not (ADR-0016, amended).
	camera::registerCameraBackends();
	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	const flow::serialize::ValueCodecs codecs = flowview::sceneCodecs();

	for (const std::string& key : factory.keys())
	{
		const std::unique_ptr<flow::Node> node = factory.create(key);
		REQUIRE(node != nullptr);
		for (std::size_t i = 0; i < node->paramCount(); ++i)
		{
			INFO(key << "." << node->param(i).name() << " : " << node->param(i).typeName());
			CHECK(codecs.find(node->param(i).type()) != nullptr);
		}
	}
}
