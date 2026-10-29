#include "viewportpane.h"

#include "overlays.h"
#include "picking.h"
#include "viewing.h"

#include <lain/gui/gizmo.h>
#include <lain/gui/gui.h>
#include <lain/log/log.h>
#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <utility>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	// What the Viewport adds to a plan's own light when Preview Light is on, and what shows where nothing
	// is drawn: a dark grey, so the grid's backdrop reads against it.
	static constexpr float kPreviewAmbient = 0.15f;
	static const image::ColorRGBf kBackground{0.02f, 0.02f, 0.025f};

	ViewportPane::ViewportPane(acm::Device& device, gui::Context& gui)
		: m_renderer(device)
	{
		m_problems = m_renderer.problems();
		if (m_problems.empty())
		{
			m_viewport.emplace(m_renderer);
			m_problems = m_viewport->problems();
		}
		// Asked when each frame is recorded, after its pre-pass has drawn (and perhaps resized) that frame's
		// texture, so the pane always shows the picture of the frame it is in (gui::FrameTexture).
		m_texture = gui.frameTexture([this](std::uint32_t frame)
									 { return m_viewport ? m_viewport->texture(frame) : acm::Texture{}; });
	}

	ViewportPane::~ViewportPane()
	{
		release();
	}

	void ViewportPane::poll(AppContext&)
	{
		for (auto it = m_jobs.begin(); it != m_jobs.end();)
		{
			if (!it->second.complete())
			{
				++it;
				continue;
			}
			rp::AssetOutcome outcome;
			try
			{
				it->second.get(&outcome);
			}
			catch (const std::exception& e)
			{
				outcome = rp::AssetOutcome{it->first, {}, {string::format("{}: {}", it->first, e.what())}};
			}
			rp::addAsset(m_assets, std::move(outcome));
			m_assetsChanged = true;
			it = m_jobs.erase(it);
		}
	}

	// A column-major float[16], as ImGuizmo reads and writes a matrix.
	static std::array<float, 16> floats(const math::Mat4d& m)
	{
		std::array<float, 16> out{};
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
				out[std::size_t(c * 4 + r)] = float(m[c][r]);
		}
		return out;
	}

	static math::Mat4d doubles(const std::array<float, 16>& m)
	{
		math::Mat4d out{1.0};
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
				out[c][r] = double(m[std::size_t(c * 4 + r)]);
		}
		return out;
	}

	void ViewportPane::navigate(const math::Vec2d& sizePixels)
	{
		const ImGuiIO& io = gui::GetIO();
		const math::Vec2d drag{io.MouseDelta.x * io.DisplayFramebufferScale.x, io.MouseDelta.y * io.DisplayFramebufferScale.y};
		if (drag.x == 0.0 && drag.y == 0.0)
			return;
		const bool left = gui::IsMouseDown(ImGuiMouseButton_Left);
		const bool pan = gui::IsMouseDown(ImGuiMouseButton_Middle) || (left && io.KeyShift);
		const bool dolly = gui::IsMouseDown(ImGuiMouseButton_Right) || (left && io.KeyCtrl);
		const camera::ImageGeometry image{std::uint32_t(sizePixels.x), std::uint32_t(sizePixels.y)};
		if (pan)
			panBy(m_orbit, drag, image);
		else if (dolly)
			dollyBy(m_orbit, (drag.x + drag.y) * 2.0 / std::max(1.0, sizePixels.y)); // the pane's height is a factor of e
		else if (left)
			orbitBy(m_orbit, -drag.x * 0.25 / io.DisplayFramebufferScale.x, drag.y * 0.25 / io.DisplayFramebufferScale.y); // the scene follows the cursor
	}

	void ViewportPane::draw(AppContext& ctx)
	{
		m_viewValid = false;
		gui::gizmo::BeginFrame();
		// The wheel zooms the view, so it must not scroll the window as well.
		if (gui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (!m_problems.empty())
			{
				gui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f), "The Viewport cannot draw on this device:");
				for (const std::string& problem : m_problems)
					gui::TextWrapped("%s", problem.c_str());
				gui::End();
				return;
			}

			// W moves and E turns what is selected; never while a text field has the keys.
			const ImGuiIO& io = gui::GetIO();
			if (!io.WantTextInput && gui::IsKeyPressed(ImGuiKey_W, false))
				m_mode = GizmoMode::Translate;
			if (!io.WantTextInput && gui::IsKeyPressed(ImGuiKey_E, false))
				m_mode = GizmoMode::Rotate;
			if (gui::RadioButton("Move (W)", m_mode == GizmoMode::Translate))
				m_mode = GizmoMode::Translate;
			gui::SameLine();
			if (gui::RadioButton("Turn (E)", m_mode == GizmoMode::Rotate))
				m_mode = GizmoMode::Rotate;
			gui::SameLine();
			gui::Checkbox("In its own axes", &m_local);
			gui::SameLine();
			gui::TextDisabled("frame %zu  ·  Alt-drag to look round, Ctrl snaps", shownFrame(ctx.document.plan, ctx.selection));

			const ImVec2 origin = gui::GetCursorScreenPos();
			const ImVec2 size{std::max(1.0f, gui::GetContentRegionAvail().x), std::max(1.0f, gui::GetContentRegionAvail().y)};
			// At the framebuffer's pixels, not the window's points: two of each a side on a Retina display.
			const math::Vec2d pixels{std::max(1.0, std::floor(double(size.x) * io.DisplayFramebufferScale.x)),
									 std::max(1.0, std::floor(double(size.y) * io.DisplayFramebufferScale.y))};
			m_view = viewOf(m_orbit, camera::ImageGeometry{std::uint32_t(pixels.x), std::uint32_t(pixels.y)});
			m_viewValid = true;
			gui::GetWindowDrawList()->AddImage(m_texture, origin, ImVec2(origin.x + size.x, origin.y + size.y));

			// The gizmo, on what is selected, over the picture. Not while Alt is held: Alt is the view's.
			rp::RigPlan& plan = ctx.document.plan;
			const rp::NominalLayout layout = rp::nominalLayout(plan);
			gui::gizmo::SetDrawlist();
			gui::gizmo::SetRect(origin.x, origin.y, size.x, size.y);
			gui::gizmo::Enable(!io.KeyAlt);
			if (const std::optional<GizmoTarget> target = gizmoTarget(plan, layout, ctx.selection))
			{
				const gui::gizmo::OPERATION op = operation(*target, m_mode);
				if (op != 0)
				{
					const GizmoMatrices matrices = gizmoMatrices(m_view);
					std::array<float, 16> matrix = floats(target->planFromTarget);
					const float translateSnap[3] = {kTranslateSnapMetres, kTranslateSnapMetres, kTranslateSnapMetres};
					const float rotateSnap[3] = {kRotateSnapDegrees, kRotateSnapDegrees, kRotateSnapDegrees};
					const float* snap = io.KeyCtrl ? (m_mode == GizmoMode::Translate ? translateSnap : rotateSnap) : nullptr;
					if (gui::gizmo::Manipulate(matrices.view.data(), matrices.projection.data(), op, m_local ? gui::gizmo::LOCAL : gui::gizmo::WORLD,
											   matrix.data(), nullptr, snap) &&
						apply(plan, layout, ctx.selection, m_mode, doubles(matrix)))
						ctx.changed();
				}
			}

			// The view's own input, where the gizmo is not: an item over the gizmo would keep it from
			// taking the click (ImGuizmo activates only when no item is hovered).
			if (!gui::gizmo::IsOver())
			{
				gui::SetCursorScreenPos(origin);
				gui::InvisibleButton("view", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
				if (gui::IsItemHovered() && io.MouseWheel != 0.0f)
					zoomBy(m_orbit, io.MouseWheel);
				if (gui::IsItemActive() && io.KeyAlt)
					navigate(pixels);
				else if (gui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
				{
					// A click picks what is under it in the scene as drawn: lain's pixel (0, 0) is the centre of
					// the top-left pixel, half a pixel in from the pane's corner.
					const math::Vec2d at{(io.MousePos.x - origin.x) * io.DisplayFramebufferScale.x - 0.5,
										 (io.MousePos.y - origin.y) * io.DisplayFramebufferScale.y - 0.5};
					ctx.selection = pick(plan, m_layout, m_scene, rayThrough(m_view, at));
				}
			}
		}
		gui::End();
	}

	void ViewportPane::frameView(const AppContext& ctx, bool selected)
	{
		if (!m_viewValid)
			return;
		const std::optional<Bounds> bounds = selected ? boundsOfSelection(ctx.document.plan, m_layout, m_fullScene, ctx.selection)
													  : boundsOfEverything(ctx.document.plan, m_layout, m_fullScene);
		if (bounds)
			m_orbit = framing(m_orbit, bounds->lower, bounds->upper, m_view.image);
	}

	void ViewportPane::update(AppContext& ctx)
	{
		if (!m_problems.empty())
			return;
		const rp::RigPlan& plan = ctx.document.plan;
		const std::size_t frame = shownFrame(plan, ctx.selection);
		lain::data::Value now = snapshot(plan);
		const bool planChanged = !m_builtFrom || *m_builtFrom != now;
		bool rebuilt = false;
		if (planChanged || frame != m_builtFrame || m_assetsChanged || m_builtPreviewLight != ctx.view.previewLight)
		{
			// The surfaces' and poles' meshes at once; everything slower on the pool, posted once each.
			rp::sceneGeometry(plan, m_assets);
			if (planChanged)
				rp::pruneAssets(plan, m_assets);
			for (rp::AssetJob& job : rp::missingAssets(plan, m_assets))
			{
				if (m_jobs.count(job.key) == 0)
					m_jobs.emplace(job.key, task::async(std::move(job.run)));
			}

			m_layout = rp::nominalLayout(plan);
			m_fullScene = rp::renderScene(plan, m_layout, m_assets, frame);
			m_fullScene.scene.background = kBackground;
			if (ctx.view.previewLight)
				m_fullScene.scene.ambient = m_fullScene.scene.ambient + image::ColorRGBf{kPreviewAmbient, kPreviewAmbient, kPreviewAmbient};

			m_builtFrom = std::move(now);
			m_builtFrame = frame;
			m_builtPreviewLight = ctx.view.previewLight;
			m_assetsChanged = false;
			rebuilt = true;
		}

		// The dollhouse, from this frame's eye: the device's copy changes only when a wall goes or comes back.
		const math::Vec3d eye = m_viewValid ? m_view.worldFromCamera.translation() : viewOf(m_orbit, {1, 1}).worldFromCamera.translation();
		std::vector<bool> cut = cutawayMask(plan, m_fullScene, eye);
		if (rebuilt || cut != m_cut)
		{
			m_scene = cutaway(plan, m_fullScene, eye);
			m_cut = std::move(cut);
			m_deviceProblems.clear();
			if (!m_gpu)
			{
				render::UploadResult uploaded = m_renderer.upload(m_scene.scene);
				m_deviceProblems = std::move(uploaded.problems);
				if (uploaded.scene)
					m_gpu.emplace(std::move(*uploaded.scene));
			}
			else
				m_deviceProblems = m_renderer.update(*m_gpu, m_scene.scene).problems;
		}

		ctx.assetsPending = m_jobs.size();
		ctx.assetProblems.clear();
		for (const auto& [key, problems] : m_assets.failed)
		{
			(void)key;
			ctx.assetProblems.insert(ctx.assetProblems.end(), problems.begin(), problems.end());
		}
		for (const std::string& problem : m_deviceProblems)
			ctx.assetProblems.push_back("the Viewport could not draw the plan: " + problem);

		// A plan opened or made is framed whole; the view is not kept with a plan.
		if (ctx.opened != m_framedFor && m_viewValid)
		{
			frameView(ctx, false);
			m_framedFor = ctx.opened;
		}
		if (ctx.view.frameAll)
			frameView(ctx, false);
		if (ctx.view.frameSelected)
			frameView(ctx, true);
		ctx.view.frameAll = false;
		ctx.view.frameSelected = false;

		OverlayOptions options;
		options.frustumDepth = ctx.view.frustumDepth;
		options.selection = ctx.selection;
		m_lines = overlays(plan, m_layout, m_fullScene, options);
		m_grid = floorGrid(plan);
	}

	void ViewportPane::record(acm::CommandBuffer& cmd, std::uint32_t frame)
	{
		if (!m_viewport || !m_gpu || !m_viewValid)
			return;
		const std::vector<std::string> problems = m_viewport->record(cmd, frame, *m_gpu, m_view, m_lines, m_grid);
		if (!problems.empty() && problems != m_recordProblems)
			log::warn("the Viewport recorded nothing: {}", problems.front());
		m_recordProblems = problems;
	}

	void ViewportPane::release()
	{
		// Each job reads only what it captured, but its result is kept by this pane, so it is waited for.
		for (auto& [key, job] : m_jobs)
		{
			(void)key;
			task::waitAll(job);
		}
		m_jobs.clear();
		m_texture = {};
		m_viewport.reset();
		m_gpu.reset();
	}
} // namespace rigplan
