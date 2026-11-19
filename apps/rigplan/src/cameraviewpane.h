#pragma once

#include "appcontext.h"
#include "lookthrough.h"

#include <lain/camera/cameramodel.h>
#include <lain/gui/context.h>
#include <lain/gui/frametexture.h>
#include <lain/render/capture.h>
#include <lain/render/renderer.h>
#include <lain/task/task.h>

#include <archimedes/acmDevice.h>
#include <archimedes/acmTexture.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// The camera view: looking through a camera (CONTEXT.md, "Capture render"), a live capture of it at
	// the frame shown, at a reduced resolution, as its sensor records it without the noise. It follows the
	// selected camera, or stays on one pinned, through its nominal or its realised model (lookthrough.h).
	//
	// It captures on the frame loop, and only when what the picture shows changed: the camera, the model
	// chosen, the frame, the plan's revision, its assets, or the resolution step, and only while the pane
	// is on screen. A capture is two waited submissions a tile (ADR-0028) and holds the queue lock the
	// window's own submit and present take, so on another thread every wait would still hold up the
	// window. Its ray table is built on the process pool, never while a gesture is under way, so a lens
	// dragged in the Inspector builds one table where the drag ends; the picture shown meanwhile is the
	// last one.
	//
	// It owns its renderer and its copy of the plan's scene on the device: uncut, with no preview light,
	// since it shows what a capture holds. A picture is a new texture each time, never one rewritten in
	// place, since a frame still in flight may be drawing the last (gui::FrameTexture holds it while it
	// is). It releases everything in release(), before the gui context and the device go.
	class CameraViewPane
	{
	public:
		CameraViewPane(acm::Device& device, lain::gui::Context& gui);
		CameraViewPane(const CameraViewPane&) = delete;
		CameraViewPane& operator=(const CameraViewPane&) = delete;
		~CameraViewPane();

		// The pane: the camera and model chosen, the pin, what is wrong, and the picture.
		void draw(AppContext& ctx);

		// After every pane has drawn and the plan's assets are brought up to date: what it looks through
		// worked out again when the plan, the camera or the model chosen changed, its ray table posted
		// (unless a `gesture` is under way) or kept, its scene brought to the device, and a capture made
		// when anything it shows changed. Captures on the frame loop.
		void update(AppContext& ctx, bool gesture);

		// Waits for the ray table being built, then lets go of the GPU state, before the device goes.
		void release();

	private:
		// What a picture was captured from: when any of it changes, the next update captures again.
		struct CaptureKey
		{
			lain::core::Uuid camera;
			LookModel model = LookModel::Nominal;
			std::uint64_t plan = ~std::uint64_t(0);
			std::uint64_t assets = ~std::uint64_t(0);
			std::size_t frame = 0;
			std::uint32_t divisor = 0;
			std::uint64_t table = 0; // which ray table it went through

			bool operator==(const CaptureKey& rhs) const
			{
				return camera == rhs.camera && model == rhs.model && plan == rhs.plan && assets == rhs.assets && frame == rhs.frame &&
					   divisor == rhs.divisor && table == rhs.table;
			}
			bool operator!=(const CaptureKey& rhs) const { return !(*this == rhs); }
		};

		void collectTable();
		void capture(const CaptureKey& key);

		acm::Device& m_device;
		lain::render::Renderer m_renderer;
		std::vector<std::string> m_problems; // why it cannot capture at all on this device
		LookChoice m_choice;

		// What it looks through, and the camera, model and plan revision that was worked out for.
		std::optional<LookThrough> m_look;
		CaptureKey m_lookFor;
		std::optional<lain::camera::CameraModel> m_atStep; // its model at the step it is shown at
		std::uint32_t m_divisor = 1;
		std::vector<std::string> m_stepProblems;

		// The ray table on the device, the model it was built for, and the one being built on the pool.
		std::optional<lain::render::GpuRayTable> m_table;
		std::optional<lain::camera::CameraModel> m_tableModel;
		std::uint64_t m_tableVersion = 0;
		std::optional<lain::task::Handle<lain::render::RayTableResult>> m_building;
		std::optional<lain::camera::CameraModel> m_buildingModel;
		std::optional<lain::camera::CameraModel> m_refusedModel; // whose table was refused, not asked for again
		std::vector<std::string> m_tableProblems;

		// Its copy of the scene on the device, and what it was built from.
		std::optional<lain::render::GpuScene> m_gpu;
		CaptureKey m_sceneFor;
		std::vector<std::string> m_sceneProblems; // an asset it lacks, or the device refusing it

		// The picture, what it was captured from, and how long the last capture took.
		acm::Texture m_picture;
		lain::camera::ImageGeometry m_pictureSize;
		lain::gui::FrameTexture m_texture;
		std::optional<CaptureKey> m_captured;
		std::vector<std::string> m_captureProblems;
		double m_captureMilliseconds = 0;

		// Set by draw(): whether the pane is on screen, and its picture's area in pixels.
		bool m_visible = false;
		std::uint32_t m_paneWidth = 1;
		std::uint32_t m_paneHeight = 1;
	};
} // namespace rigplan
