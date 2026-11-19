#pragma once

#include "appcontext.h"
#include "visibilityschedule.h"

#include <lain/gui/context.h>
#include <lain/gui/frametexture.h>
#include <lain/mesh/mesh.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>

#include <archimedes/acmDevice.h>
#include <archimedes/acmTexture.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// The visibility map (CONTEXT.md, "Visibility map"): a top-down picture of how many cameras see each
	// cell of the floor, or of a plane at a chosen height, from the plan's nominal poses, over what never
	// moves (lain::rigplan::staticScene). A legend says what each colour counts, and the cell under the
	// pointer says its count and names its cameras.
	//
	// It draws each camera's cube faces on the frame loop, a few milliseconds a frame and only while the
	// pane is on screen, and counts each camera on the process pool (VisibilitySchedule). A map is made
	// again when the plan, the plane or the static scene changes (a prop's mesh landing, not a bake), never
	// during a gesture; the last one stays meanwhile. It owns its renderer and its copy of the static scene
	// on the device, and releases them in release(), before the gui context and the device go.
	class VisibilityPane
	{
	public:
		VisibilityPane(acm::Device& device, lain::gui::Context& gui);
		VisibilityPane(const VisibilityPane&) = delete;
		VisibilityPane& operator=(const VisibilityPane&) = delete;
		~VisibilityPane();

		// The pane: the plane chosen, how far the map has got, the picture, the legend, the hover.
		void draw(AppContext& ctx);

		// After every pane has drawn and the plan's assets are brought up to date: the map wanted worked out,
		// and while the pane is on screen, its faces drawn for a few milliseconds and its picture made again
		// when the map shown changed. Never waits on the pool.
		void update(AppContext& ctx, bool gesture);

		// Waits for the counts still running, then lets go of the GPU state, before the device goes.
		void release();

	private:
		// What a static scene draws, to tell a scene that changed from one that did not: a bake landing
		// changes nothing in it, a prop's mesh landing does.
		struct Fingerprint
		{
			std::vector<const lain::mesh::Mesh*> meshes; // each instance's
			std::vector<lain::math::Mat4d> placements;
			bool operator==(const Fingerprint& rhs) const { return meshes == rhs.meshes && placements == rhs.placements; }
		};

		void makePicture();

		acm::Device& m_device;
		lain::render::Renderer m_renderer;
		std::vector<std::string> m_problems; // why it cannot draw at all on this device
		VisibilitySchedule m_schedule;

		// The plane counted over: the floor, or a height above it.
		bool m_atHeight = false;
		float m_height = 1.0f;

		// The revision of the map wanted, and what it was worked out from.
		std::uint64_t m_revision = 0;
		std::uint64_t m_planFor = ~std::uint64_t(0);
		std::uint64_t m_assetsFor = ~std::uint64_t(0);
		bool m_atHeightFor = false;
		float m_heightFor = 0;
		lain::rigplan::SceneResult m_scene; // the static scene of the revision wanted
		Fingerprint m_fingerprint;

		// The static scene on the device, and the revision it is of.
		std::optional<lain::render::GpuScene> m_gpu;
		std::uint64_t m_uploadedFor = 0;
		std::vector<std::string> m_deviceProblems;

		// The picture, and the map version and scale it shows.
		acm::Texture m_picture;
		lain::gui::FrameTexture m_texture;
		std::uint64_t m_pictureFor = ~std::uint64_t(0);
		int m_pictureScale = 0;
		int m_scaleWanted = 1;

		bool m_visible = false;
	};
} // namespace rigplan
