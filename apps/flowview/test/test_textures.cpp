// The textures flowview's sequence player and preview cache draw, through a real gui::Context on the
// live driver. Opt-in (LAIN_GUI_SMOKE=1), as lain::gui's windowed cases are, so a machine with no
// display or driver stays green; everything else in test-flowview is driver-free.
//
// Both used to rewrite a texture in place when the next picture was the same size, while up to
// acm::Renderer::MaxFramesInFlight frames that drew it could still be sampling it: a write-after-read
// hazard (WORK.md's eighth known defect). Nothing here can see the hazard itself — there is no
// validation layer, and MoltenVK does not fault on it — so the cases pin the arrangement that rules it
// out: a changed picture is drawn through a NEW texture, a descriptor of its own, and an unchanged one
// keeps the texture it has, which is what shows the case can tell the two apart.

#include "graphio.h" // registerSceneSerialization — the port types the scene declares
#include "pinkey.h"
#include "previewcache.h"
#include "sequenceview.h"
#include "valueviews.h"

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/portvalue.h>
#include <lain/gui/context.h>
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/open.h>
#include <lain/io/sequence/openers.h>
#include <lain/media/framesequence.h>
#include <lain/media/operations.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace lain;
namespace fs = std::filesystem;

// A window that owns a gui::Context and runs one probe in its first frame, handed in by the case. A
// probe RECORDS what it saw and asserts nothing: a failing REQUIRE throws, and a throw out of
// Application::run() would leave the app running at ~Application. The case asserts afterwards.
struct ProbeWindow : app::WindowDelegate
{
	std::function<void(gui::Context&)> probe;
	std::unique_ptr<gui::Context> gui;
	bool probed = false;

	bool onInit(app::Window& window) override
	{
		gui = std::make_unique<gui::Context>(window.app(), window); // no ini: nothing written to disk
		return true;
	}

	void onRender(app::Window&, const app::TimeState&) override
	{
		if (probed)
			return;
		probe(*gui);
		probed = true;
	}

	void onShutdown(app::Window&) override { gui.reset(); }
};

struct ProbeApp : app::ApplicationDelegate
{
	ProbeWindow window;

	bool onStart(app::Application& app) override
	{
		app::WindowSpec spec;
		spec.title = "flowview textures";
		spec.width = 640;
		spec.height = 480;
		app.createWindow(spec, window);
		return true;
	}

	// The frame that asks to stop is still rendered, so exiting in frame 0 runs the probe once.
	void onUpdate(app::Application& app, const app::TimeState&, const app::InputState&) override { app.exit(); }
};

// Initialise, run and shut down, as main does.
static void runProbe(std::function<void(gui::Context&)> probe)
{
	ProbeApp delegate;
	delegate.window.probe = std::move(probe);
	app::Application app(delegate, {"test-flowview", {0, 0, 0}});
	char arg0[] = "test-flowview";
	char* argv[] = {arg0};
	REQUIRE_FALSE(app.initialise(1, argv).has_value()); // nothing started yet, so a throw here is safe
	const int status = app.run();
	app.shutdown(); // before asserting: a throw now must not leave the app running
	REQUIRE(status == 0);
	REQUIRE(delegate.window.probed);
}

// One ImGui frame of `draw`, in a window filling the display, ended without being recorded; and every
// texture its draw commands named that ImGui does not manage itself (its font atlas is the rest). A
// texture is named by its descriptor, so two pictures through one texture name the same one.
static std::vector<ImTextureID> drawnTextures(gui::Context& ctx, const std::function<void()>& draw)
{
	ctx.newFrame();
	gui::SetNextWindowPos(math::Vec2f{0.0f, 0.0f});
	gui::SetNextWindowSize(gui::GetIO().DisplaySize);
	gui::Begin("probe", nullptr, ImGuiWindowFlags_NoDecoration);
	draw();
	gui::End();
	gui::Render();
	std::vector<ImTextureID> named;
	for (const ImDrawList* list : gui::GetDrawData()->CmdLists)
	{
		for (const ImDrawCmd& cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback == nullptr && cmd.TexRef._TexData == nullptr)
				named.push_back(cmd.TexRef._TexID);
		}
	}
	return named;
}

// A 4 x 4 grey picture, every one the same size and format so nothing about a new one would need a
// texture of another shape.
static image::Image grey(std::uint8_t level)
{
	image::Image made{4, 4, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
	for (std::size_t i = 0; i < made.byteSize(); ++i)
		made.data()[i] = level;
	return made;
}

TEST_CASE("the sequence player shows each new frame through a new texture", "[flowview][gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// A folder of stills, opened through the production opener seam, as the gui opens one.
	io::image::registerImageCodecs();
	io::sequence::registerSequenceOpeners();
	flowview::registerSceneSerialization();
	const fs::path dir = lain::testing::scratchDir() / "textures-sequence";
	fs::remove_all(dir);
	fs::create_directories(dir);
	for (int i = 0; i < 3; ++i)
		REQUIRE(io::image::save((dir / ("f" + std::to_string(i) + ".png")).string(), grey(static_cast<std::uint8_t>(20 + i * 60))));
	const std::optional<media::FrameSequence> sequence = io::sequence::open(dir.string());
	REQUIRE(sequence);
	REQUIRE(sequence->size() == 3);

	// The transport stays on position 0 (it is paused), so what changes the frame shown is the pin
	// being rebound to a clip that starts later: the view's own rule for "this is a different frame".
	const auto valueOf = [](const media::FrameSequence& shown)
	{
		flow::PortValue value;
		value.set(shown);
		return value;
	};
	const flow::PortValue first = valueOf(*sequence);
	const flow::PortValue second = valueOf(media::clip(*sequence, 1, 2));
	const flow::PortValue third = valueOf(media::clip(*sequence, 2, 1));

	std::vector<std::vector<ImTextureID>> drawn;
	runProbe([&](gui::Context& ctx)
			 {
		flowview::SequenceView view;
		const auto show = [&](const flow::PortValue& value)
		{ drawn.push_back(drawnTextures(ctx, [&] { view.draw(value, nullptr, math::Vec2f{320.0f, 240.0f}, ctx); })); };
		show(first);
		show(first); // the same frame again: nothing to decode, and nothing new to draw through
		show(second);
		show(third); });

	REQUIRE(drawn.size() == 4);
	for (std::size_t i = 0; i < drawn.size(); ++i)
	{
		INFO("draw " << i);
		REQUIRE(drawn[i].size() == 1); // the frame, and nothing else drawn from a texture of the view's
		CHECK(drawn[i][0] != ImTextureID_Invalid);
	}
	CHECK(drawn[1][0] == drawn[0][0]);
	CHECK(drawn[2][0] != drawn[1][0]);
	CHECK(drawn[3][0] != drawn[2][0]);
}

TEST_CASE("the preview cache shows a changed picture through a new texture", "[flowview][gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// An image on a boundary pin, bound as the gui binds one, and its thumbnail as the panes find it.
	flow::Graph graph;
	const flow::PortId pin = graph.boundaryInputNode().addBoundary<image::Image>("image");
	const flow::PortAddress address{graph.boundaryInputNode().id(), pin};
	flow::Evaluation evaluation(graph);
	evaluation.prepare(graph);
	const flowview::PinKey key{flow::EvalPath{}, address};
	flowview::ValueViews views;
	flowview::registerBuiltinValueViews(views);
	const auto bind = [&](std::uint8_t level)
	{
		flow::PortValue value;
		value.set(grey(level));
		evaluation.bind(address, std::move(value));
	};

	std::vector<std::optional<ImTextureID>> shown; // the thumbnail's texture after each refresh
	runProbe([&](gui::Context& ctx)
			 {
		flowview::PreviewCache cache;
		const auto refresh = [&]
		{
			cache.markDirty();
			cache.refreshIfDirty(flow::EvalPath{}, graph, evaluation, views, ctx);
			std::optional<ImTextureID> texture;
			if (const gui::Texture* thumbnail = cache.find(key))
				texture = static_cast<ImTextureRef>(*thumbnail).GetTexID();
			shown.push_back(texture);
		};
		bind(20);
		refresh();
		refresh(); // the same payload: its thumbnail stands
		bind(80);  // another picture of the same size and format
		refresh();
		cache.clear(); });

	REQUIRE(shown.size() == 3);
	for (std::size_t i = 0; i < shown.size(); ++i)
	{
		INFO("refresh " << i);
		REQUIRE(shown[i].has_value());
	}
	CHECK(*shown[1] == *shown[0]);
	CHECK(*shown[2] != *shown[1]);
}
