// Windowed tests for gui::Texture through a real gui::Context, on the live driver. Opt-in
// (LAIN_GUI_SMOKE=1) like lain::app's windowed cases, so GUI-less / driver-less CI stays green.
//
// A Texture's descriptor is archimedes', allocated against a layout built to match ImGui's own
// texture set, rather than one from ImGui's descriptor pool — so that releasing a Texture defers
// destroying it past every frame that could have drawn it, which ImGui_ImplVulkan_RemoveTexture
// (an immediate vkFreeDescriptorSets) did not. That early free is a Vulkan usage error with no
// symptom on MoltenVK and no validation layer here to report it, so neither case below watches for
// it directly. They pin the two things the arrangement rests on instead: that such a descriptor
// really does draw through ImGui's pipeline, and that it really is not ImGui's.

#include "lain/gui/context.h"
#include "lain/gui/gui.h"

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/image/image.h>

#include <archimedes/archimedes.h>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using namespace lain;

// A solid RGBA8 image — the only format a gui::Texture holds, so no conversion is in play.
static image::Image solid(int size, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
	image::Image img(size, size, image::PixelFormat::RGBA8);
	std::uint8_t* bytes = img.data();
	for (std::size_t i = 0; i < img.byteSize(); i += 4)
	{
		bytes[i + 0] = r;
		bytes[i + 1] = g;
		bytes[i + 2] = b;
		bytes[i + 3] = 255;
	}
	return img;
}

// A window that owns a gui::Context and does one probe in its first frame, handed in by the case.
// A probe RECORDS what it saw and asserts nothing: a failing REQUIRE throws, and a throw out of
// Application::run() would leave the app running at ~Application. The case asserts afterwards.
struct ProbeWindow : app::WindowDelegate
{
	std::function<void(app::Window&, gui::Context&)> probe;
	std::unique_ptr<gui::Context> gui;
	bool probed = false;

	bool onInit(app::Window& window) override
	{
		gui = std::make_unique<gui::Context>(window.app(), window); // no ini: nothing written to disk
		return true;
	}

	void onRender(app::Window& window, const app::TimeState&) override
	{
		if (probed)
			return;
		probe(window, *gui);
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
		spec.title = "lain::gui texture";
		spec.width = 320;
		spec.height = 240;
		app.createWindow(spec, window);
		return true;
	}

	// The frame that asks to stop is still rendered, so exiting in frame 0 runs the probe once.
	void onUpdate(app::Application& app, const app::TimeState&, const app::InputState&) override { app.exit(); }
};

// Initialise, run and shut down, as main does.
static void runProbe(std::function<void(app::Window&, gui::Context&)> probe)
{
	ProbeApp delegate;
	delegate.window.probe = std::move(probe);
	app::Application app(delegate, {"test-gui", {0, 0, 0}});
	char arg0[] = "test-gui";
	char* argv[] = {arg0};
	REQUIRE_FALSE(app.initialise(1, argv).has_value()); // nothing started yet, so a throw here is safe
	const int status = app.run();
	app.shutdown(); // before asserting: a throw now must not leave the app running
	REQUIRE(status == 0);
	REQUIRE(delegate.window.probed);
}

TEST_CASE("a texture's archimedes descriptor draws through ImGui's pipeline", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// Magenta appears nowhere in ImGui's default style, so reading it back means THIS texture was
	// sampled through the descriptor archimedes allocated — a descriptor never written reads back as
	// the window's background instead. What this does NOT guard is the layout staying identical to
	// ImGui's, which Vulkan requires for the bind: MoltenVK draws correctly through a stage-mask and
	// even a descriptor-type mismatch (both measured), so drift would pass here. That identity is
	// held by the comment on Context's layout, and would need a validation layer to test.
	bool created = false;				// the texture came back valid
	bool rendered = false;				// the offscreen draw was submitted and completed
	std::optional<std::uint32_t> drawn; // the read-back pixel, packed 0xRRGGBB
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		acm::Device& device = window.app().device();
		gui::Texture texture = ctx.createTexture(solid(64, 255, 0, 255));
		created = texture.valid();
		if (!created)
			return;

		ctx.newFrame();
		gui::SetNextWindowPos(math::Vec2f{0.0f, 0.0f});
		gui::SetNextWindowSize(gui::GetIO().DisplaySize);
		gui::Begin("probe", nullptr, ImGuiWindowFlags_NoDecoration);
		gui::Image(texture, math::Vec2f{64.0f, 64.0f});
		const math::Vec2f centre = (math::Vec2f(gui::GetItemRectMin()) + math::Vec2f(gui::GetItemRectMax())) * 0.5f;
		gui::End();

		// Rendered OFFSCREEN, into a target of the swapchain's own format (ImGui's pipeline was
		// built for it) and size, so the result can be read back — a swapchain image cannot be.
		const acm::Extent2D extent = window.swapChain().extent();
		acm::Texture target = device.createTexture(window.swapChain().format().format, extent);
		acm::RenderTarget rendering = device.createRenderTarget(target, acm::RenderTargetConfig{acm::RenderTargetFinish::CopySrc});
		acm::Buffer readback = device.createBuffer(std::size_t{extent.width} * extent.height * 4, acm::BufferUsage::TransferDst);
		if (!rendering.valid() || !readback.valid())
			return;
		bool began = false;
		const acm::Error submitted = device.submitSync([&](acm::CommandBuffer& cmd)
													   {
			began = !cmd.beginRendering(rendering);
			if (!began)
				return;
			cmd.setViewportAndScissor(extent);
			ctx.render(cmd);
			cmd.endRendering();
			cmd.copyTextureToBuffer(target, readback); });
		rendered = began && !submitted;
		if (!rendered)
			return;

		// The item rect is in ImGui's logical units; the target is in framebuffer pixels.
		const math::Vec2f display = gui::GetIO().DisplaySize;
		const auto x = static_cast<std::size_t>(centre.x * static_cast<float>(extent.width) / display.x);
		const auto y = static_cast<std::size_t>(centre.y * static_cast<float>(extent.height) / display.y);
		const auto* px = static_cast<const std::uint8_t*>(readback.map());
		if (px == nullptr)
			return;
		const std::size_t at = (y * extent.width + x) * 4; // a UNORM swapchain format is [B,G,R,A]
		drawn = (std::uint32_t{px[at + 2]} << 16) | (std::uint32_t{px[at + 1]} << 8) | px[at + 0];
		readback.unmap(); });

	REQUIRE(created);
	REQUIRE(rendered);
	REQUIRE(drawn.has_value());
	CHECK(*drawn == 0xFF00FFu);
}

TEST_CASE("a context holds more textures than ImGui's descriptor pool", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// ImGui's pool is sized for its own font atlas alone, so a Texture that drew its descriptor
	// from it would run out within a handful — which is exactly what a slide back to
	// ImGui_ImplVulkan_AddTexture, and its immediate free, would look like. 72 is past the 64 the
	// pool once held, too.
	constexpr int kCount = 72;
	int valid = 0;
	runProbe([&](app::Window&, gui::Context& ctx)
			 {
		std::vector<gui::Texture> textures;
		for (int i = 0; i < kCount; ++i)
			textures.push_back(ctx.createTexture(solid(4, 0, 255, 0)));
		for (const gui::Texture& texture : textures)
			valid += texture.valid() ? 1 : 0; });

	CHECK(valid == kCount);
}
