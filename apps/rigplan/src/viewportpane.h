#pragma once

#include "appcontext.h"
#include "assetjobs.h"
#include "gizmo.h"
#include "orbit.h"

#include <lain/data/value.h>
#include <lain/gui/context.h>
#include <lain/gui/frametexture.h>
#include <lain/render/renderer.h>
#include <lain/render/viewport.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>

#include <archimedes/acmCommandBuffer.h>
#include <archimedes/acmDevice.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// The Viewport: the designer's own view of the plan as it was meant (CONTEXT.md, "Viewport"), drawn on
	// the device through lain::render and shown through a frame-resolved texture (gui::FrameTexture), with
	// its overlays and the floor grid over it. Navigated Maya style: Alt+LMB orbits, Alt+MMB or
	// Alt+Shift+LMB pans, Alt+RMB or Alt+Ctrl+LMB dollies, and the wheel zooms. A click picks, and
	// ImGuizmo moves what is selected (W moves, E turns, Ctrl snaps).
	//
	// A room is drawn as a dollhouse: each of its surfaces the eye is outside of is left out (cutaway()),
	// so the rig inside is seen from any side. It owns the renderer, the plan's scene on the device and
	// the lain::render::Viewport, and releases
	// them in release(), before the device goes. And it never waits on asset work (ADR-0025's rule,
	// carried to rigplan): a bake, a board drawn or a mesh read runs on the process pool, and until it
	// lands the Viewport draws what it had, or a surface a flat mid grey.
	class ViewportPane
	{
	public:
		ViewportPane(acm::Device& device, lain::gui::Context& gui);
		ViewportPane(const ViewportPane&) = delete;
		ViewportPane& operator=(const ViewportPane&) = delete;
		~ViewportPane();

		// Frame start: keep what the asset jobs made since the last frame. Never waits.
		void poll(AppContext& ctx);

		// The pane: the picture, and what the mouse and the keys do to the view and the plan.
		void draw(AppContext& ctx);

		// After every pane has drawn: when the plan, the frame shown or the assets changed, the scene is
		// built again and brought to the device and what the plan no longer draws is pruned; the missing
		// asset jobs are posted, unless a `gesture` is under way (AssetJobs); then the overlays for this
		// frame.
		void update(AppContext& ctx, bool gesture);

		// The frame's pre-pass: the view recorded into its texture, before the frame's draw pass shows it.
		void record(acm::CommandBuffer& cmd, std::uint32_t frame);

		// Waits for the asset jobs still running, then lets go of the GPU state, before the device goes.
		void release();

	private:
		void frameView(const AppContext& ctx, bool selected); // Frame All, or Frame Selected
		void navigate(const lain::math::Vec2d& sizePixels);

		lain::render::Renderer m_renderer;
		std::optional<lain::render::GpuScene> m_gpu;
		std::optional<lain::render::Viewport> m_viewport;
		lain::gui::FrameTexture m_texture;
		std::vector<std::string> m_problems; // why the Viewport cannot draw at all

		// The plan the scene was built from, the frame it shows, and what it drew.
		std::optional<lain::data::Value> m_builtFrom;
		std::size_t m_builtFrame = 0;
		bool m_builtPreviewLight = false;
		lain::rigplan::SceneAssets m_assets;
		bool m_assetsChanged = true;
		lain::rigplan::NominalLayout m_layout;
		// The plan's whole scene, which framing and the selection's outline read, and the cutaway of it from
		// the eye, which is drawn and picked: built again only when a wall goes or comes back.
		lain::rigplan::SceneResult m_fullScene;
		lain::rigplan::SceneResult m_scene;
		std::vector<bool> m_cut;
		AssetJobs m_jobs;
		std::vector<std::string> m_deviceProblems; // why the scene did not reach the device
		std::vector<std::string> m_recordProblems; // why the last frame recorded nothing

		// The view, and what it draws over the scene this frame.
		Orbit m_orbit;
		lain::render::View m_view;
		bool m_viewValid = false;
		std::vector<lain::render::Segment> m_lines;
		lain::render::Grid m_grid;
		std::uint64_t m_framedFor = ~std::uint64_t(0); // the AppContext::opened it framed all of

		GizmoMode m_mode = GizmoMode::Translate;
		bool m_local = false;
	};
} // namespace rigplan
