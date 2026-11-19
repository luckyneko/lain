// Windowed tests for gui::Texture and gui::FrameTexture through a real gui::Context, on the live
// driver. Opt-in (LAIN_GUI_SMOKE=1) like lain::app's windowed cases, so GUI-less / driver-less CI
// stays green.
//
// A Texture's descriptor is archimedes', allocated against a layout built to match ImGui's own
// texture set, rather than one from ImGui's descriptor pool — so that releasing a Texture defers
// destroying it past every frame that could have drawn it, which ImGui_ImplVulkan_RemoveTexture
// (an immediate vkFreeDescriptorSets) did not. That early free is a Vulkan usage error with no
// symptom on MoltenVK and no validation layer here to report it, so neither of the first and last
// cases watches for it directly. They pin the two things the arrangement rests on instead: that such
// a descriptor really does draw through ImGui's pipeline, and that it really is not ImGui's.
//
// A FrameTexture is resolved when the frame is recorded (frametexture.h), so the cases between ask
// what was drawn: the texture of the frame being recorded, the one there is when it is recorded, and
// the window's background when there is none.

#include "lain/gui/context.h"
#include "lain/gui/frametexture.h"
#include "lain/gui/gui.h"

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/image/image.h>

#include <archimedes/archimedes.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
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

// What one ImGui frame drew: the pixel at the centre of its one image, and one beside the image on the
// window's background, each packed 0xRRGGBB; and the texture id the image's draw command carried when
// it was recorded, which for a FrameTexture is what its stand-in was resolved to.
struct Drawn
{
	std::uint32_t centre = 0;
	std::uint32_t beside = 0;
	ImTextureID texture = ImTextureID_Invalid;
};

// Where a probe's frames are rendered OFFSCREEN: a target of the swapchain's own format (ImGui's
// pipeline was built for it) and size, and a buffer to read it back through, since a swapchain image
// cannot be read. Made once per probe, so that drawing a frame allocates no texture of its own.
struct Offscreen
{
	acm::Texture target;
	acm::RenderTarget rendering;
	acm::Buffer readback;
	acm::Extent2D extent{};

	bool make(app::Window& window)
	{
		acm::Device& device = window.app().device();
		extent = window.swapChain().extent();
		target = device.createTexture(window.swapChain().format().format, extent);
		rendering = device.createRenderTarget(target, acm::RenderTargetConfig{acm::RenderTargetFinish::CopySrc});
		readback = device.createBuffer(std::size_t{extent.width} * extent.height * 4, acm::BufferUsage::TransferDst);
		return rendering.valid() && readback.valid();
	}
};

// One ImGui frame whose window shows `texture` at 64 x 64, rendered into `offscreen` for frame `frame`
// and read back. `between` runs after the UI is built and before the frame is recorded, where a
// window's pre-pass would draw what a FrameTexture shows. Nothing when a step failed.
static std::optional<Drawn> drawOne(app::Window& window, gui::Context& ctx, Offscreen& offscreen, ImTextureRef texture,
									std::uint32_t frame, const std::function<void()>& between = {})
{
	acm::Device& device = window.app().device();
	ctx.newFrame();
	gui::SetNextWindowPos(math::Vec2f{0.0f, 0.0f});
	gui::SetNextWindowSize(gui::GetIO().DisplaySize);
	gui::Begin("probe", nullptr, ImGuiWindowFlags_NoDecoration);
	gui::Image(texture, math::Vec2f{64.0f, 64.0f});
	const math::Vec2f min = gui::GetItemRectMin();
	const math::Vec2f max = gui::GetItemRectMax();
	gui::End();
	if (between)
		between();

	const acm::Extent2D extent = offscreen.extent;
	bool began = false;
	const acm::Error submitted = device.submitSync([&](acm::CommandBuffer& cmd)
												   {
		began = !cmd.beginRendering(offscreen.rendering);
		if (!began)
			return;
		cmd.setViewportAndScissor(extent);
		ctx.render(cmd, frame);
		cmd.endRendering();
		cmd.copyTextureToBuffer(offscreen.target, offscreen.readback); });
	if (!began || submitted)
		return std::nullopt;

	const auto* px = static_cast<const std::uint8_t*>(offscreen.readback.map());
	if (px == nullptr)
		return std::nullopt;
	// The item rect is in ImGui's logical units; the target is in framebuffer pixels.
	const math::Vec2f display = gui::GetIO().DisplaySize;
	auto at = [&](math::Vec2f point)
	{
		const auto x = static_cast<std::size_t>(point.x * static_cast<float>(extent.width) / display.x);
		const auto y = static_cast<std::size_t>(point.y * static_cast<float>(extent.height) / display.y);
		const std::size_t i = (y * extent.width + x) * 4; // a UNORM swapchain format is [B,G,R,A]
		return (std::uint32_t{px[i + 2]} << 16) | (std::uint32_t{px[i + 1]} << 8) | px[i + 0];
	};
	const math::Vec2f centre = (min + max) * 0.5f;
	Drawn drawn{at(centre), at(math::Vec2f{max.x + 16.0f, centre.y})};
	offscreen.readback.unmap();
	// The draw data stays as Context::render left it until the next frame. The image is the frame's one
	// command not drawn from a texture ImGui manages (its font atlas).
	for (const ImDrawList* list : gui::GetDrawData()->CmdLists)
	{
		for (const ImDrawCmd& cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback == nullptr && cmd.TexRef._TexData == nullptr && drawn.texture == ImTextureID_Invalid)
				drawn.texture = cmd.TexRef._TexID;
		}
	}
	return drawn;
}

// A texture of one colour, uploaded straight through archimedes as a render target's would be made:
// what a FrameTexture's provider answers, which no gui::Texture wraps.
static acm::Texture solidTexture(acm::Device& device, int size, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
	const image::Image pixels = solid(size, r, g, b);
	acm::Texture texture = device.createTexture(acm::Format::R8G8B8A8_Unorm,
												acm::Extent2D{static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size)});
	if (texture.valid())
		texture.upload(pixels.data(), pixels.byteSize()); // synchronous; leaves it SHADER_READ_ONLY
	return texture;
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
	std::optional<std::uint32_t> drawn; // the read-back pixel, packed 0xRRGGBB
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		gui::Texture texture = ctx.createTexture(solid(64, 255, 0, 255));
		created = texture.valid();
		Offscreen offscreen;
		if (!created || !offscreen.make(window))
			return;
		if (const std::optional<Drawn> frame = drawOne(window, ctx, offscreen, texture, 0))
			drawn = frame->centre; });

	REQUIRE(created);
	REQUIRE(drawn.has_value());
	CHECK(*drawn == 0xFF00FFu);
}

TEST_CASE("a frame-resolved texture draws the texture of the frame being recorded", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// A render::Viewport keeps a texture per frame in flight, and the pane showing it is built before
	// the frame index is known. So the provider is asked in Context::render, for the frame it is given.
	std::vector<std::optional<Drawn>> drawn;
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		acm::Device& device = window.app().device();
		const acm::Texture red = solidTexture(device, 64, 255, 0, 0);
		const acm::Texture blue = solidTexture(device, 64, 0, 0, 255);
		const gui::FrameTexture shown = ctx.frameTexture([&](std::uint32_t frame) { return frame == 0 ? red : blue; });
		Offscreen offscreen;
		if (!offscreen.make(window))
			return;
		for (std::uint32_t frame : {0u, 1u, 0u, 1u})
			drawn.push_back(drawOne(window, ctx, offscreen, shown, frame)); });

	REQUIRE(drawn.size() == 4);
	const std::uint32_t expected[] = {0xFF0000u, 0x0000FFu, 0xFF0000u, 0x0000FFu};
	for (std::size_t i = 0; i < drawn.size(); ++i)
	{
		INFO("render " << i);
		REQUIRE(drawn[i].has_value());
		CHECK(drawn[i]->centre == expected[i]);
	}
}

TEST_CASE("a frame-resolved texture replaced after the UI is built draws the replacement", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// What a viewport's pre-pass does when its pane changed size: it makes that frame's target again,
	// after the pane drew the stand-in. The replacement is of another size and colour, and each render
	// replaces it again, so an answer kept from an earlier render shows.
	std::vector<std::optional<Drawn>> drawn;
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		acm::Device& device = window.app().device();
		acm::Texture current = solidTexture(device, 64, 255, 0, 0);
		const gui::FrameTexture shown = ctx.frameTexture([&](std::uint32_t) { return current; });
		Offscreen offscreen;
		if (!offscreen.make(window))
			return;
		drawn.push_back(drawOne(window, ctx, offscreen, shown, 0)); // red, as the UI was built
		drawn.push_back(drawOne(window, ctx, offscreen, shown, 0, [&] { current = solidTexture(device, 32, 0, 255, 0); }));
		drawn.push_back(drawOne(window, ctx, offscreen, shown, 0, [&] { current = solidTexture(device, 48, 0, 0, 255); })); });

	REQUIRE(drawn.size() == 3);
	REQUIRE(drawn[0].has_value());
	REQUIRE(drawn[1].has_value());
	REQUIRE(drawn[2].has_value());
	CHECK(drawn[0]->centre == 0xFF0000u);
	CHECK(drawn[1]->centre == 0x00FF00u);
	CHECK(drawn[2]->centre == 0x0000FFu);
}

TEST_CASE("a texture released and made again is drawn, not the one it replaced", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// The Context keeps a descriptor per texture, keyed by the texture's backend object. archimedes
	// keeps those objects in pool slots and hands a freed slot out again, last freed first, so a
	// texture made just after another is destroyed takes its address. Here each render releases the
	// texture it showed and waits for the device, which destroys it at once (archimedes otherwise
	// defers that until its submissions are done), then makes the next, so only that texture churns
	// the pool. The case checks that an address really did come back, or it would prove nothing.
	//
	// The pixels alone cannot see a stale descriptor here. On MoltenVK the new texture's object is
	// made where the destroyed one's was, so a descriptor left pointing at the old one draws the new
	// picture by accident (measured: every colour right with the cache holding nothing). What can be
	// seen is the descriptor itself: the Context holds the texture it last drew while that frame may
	// still be running, so the next texture cannot take its place, and gets a descriptor of its own.
	// Consecutive renders therefore draw through different descriptors.
	const std::uint32_t colours[] = {0xFF0000u, 0x00FF00u, 0x0000FFu, 0xFFFF00u, 0x00FFFFu, 0xFF00FFu, 0x808000u, 0x008080u};
	std::vector<std::optional<Drawn>> drawn;
	int reused = 0; // renders whose texture stood where an earlier render's had
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		acm::Device& device = window.app().device();
		acm::Texture current;
		const gui::FrameTexture shown = ctx.frameTexture([&](std::uint32_t) { return current; });
		Offscreen offscreen;
		if (!offscreen.make(window))
			return;
		std::vector<const void*> addresses;
		for (std::size_t i = 0; i < std::size(colours); ++i)
		{
			const std::uint32_t colour = colours[i];
			drawn.push_back(drawOne(window, ctx, offscreen, shown, 0, [&]
									{
				current = acm::Texture{};
				device.waitIdle(); // destroys it now, so its slot is free for the next
				current = solidTexture(device, i % 2 == 0 ? 64 : 32, std::uint8_t(colour >> 16), std::uint8_t(colour >> 8), std::uint8_t(colour));
				const void* address = current.backend();
				reused += std::find(addresses.begin(), addresses.end(), address) != addresses.end() ? 1 : 0;
				addresses.push_back(address); }));
		} });

	REQUIRE(drawn.size() == std::size(colours));
	REQUIRE(reused > 0);
	for (std::size_t i = 0; i < drawn.size(); ++i)
	{
		INFO("render " << i);
		REQUIRE(drawn[i].has_value());
		CHECK(drawn[i]->centre == colours[i]);
		if (i > 0)
			CHECK(drawn[i]->texture != drawn[i - 1]->texture);
	}
}

TEST_CASE("a frame-resolved texture whose provider answers nothing draws transparent", "[gui][gpu]")
{
	if (!std::getenv("LAIN_GUI_SMOKE"))
		SKIP("set LAIN_GUI_SMOKE=1 to run the windowed cases (needs a display + driver)");

	// Nothing to show is an ordinary state (a viewport before its first record), so it draws the
	// window's own background, never a null descriptor ImGui's backend would bind. A handle that died
	// before the frame was recorded draws the same.
	std::optional<Drawn> nothing;
	std::optional<Drawn> dead;
	runProbe([&](app::Window& window, gui::Context& ctx)
			 {
		const gui::FrameTexture empty = ctx.frameTexture([](std::uint32_t) { return acm::Texture{}; });
		Offscreen offscreen;
		if (!offscreen.make(window))
			return;
		nothing = drawOne(window, ctx, offscreen, empty, 0);
		const acm::Texture red = solidTexture(window.app().device(), 64, 255, 0, 0);
		auto doomed = std::make_unique<gui::FrameTexture>(ctx.frameTexture([&](std::uint32_t) { return red; }));
		const ImTextureRef standIn = *doomed;
		dead = drawOne(window, ctx, offscreen, standIn, 0, [&] { doomed.reset(); }); });

	REQUIRE(nothing.has_value());
	CHECK(nothing->centre == nothing->beside);
	REQUIRE(dead.has_value());
	CHECK(dead->centre == dead->beside);
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
