#include "cameraviewpane.h"

#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/rigplan/rigplan.h>
#include <lain/string/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <utility>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	static const ImVec4 kWarning{0.9f, 0.7f, 0.25f, 1.0f};

	CameraViewPane::CameraViewPane(acm::Device& device, gui::Context& gui)
		: m_device(device)
		, m_renderer(device)
	{
		m_problems = m_renderer.problems();
		// Asked when each frame is recorded: the picture made last, held by the gui while a frame in flight
		// draws it, so the next one is a new texture rather than this one rewritten.
		m_texture = gui.frameTexture([this](std::uint32_t)
									 { return m_picture; });
	}

	CameraViewPane::~CameraViewPane()
	{
		release();
	}

	void CameraViewPane::draw(AppContext& ctx)
	{
		m_visible = false;
		if (gui::Begin("Camera View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			m_visible = true;
			if (!m_problems.empty())
			{
				gui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f), "The camera view cannot capture on this device:");
				for (const std::string& problem : m_problems)
					gui::TextWrapped("%s", problem.c_str());
				gui::End();
				return;
			}

			// The model, the pin, and which camera at which step.
			if (gui::RadioButton("Nominal", m_choice.model == LookModel::Nominal))
				m_choice.model = LookModel::Nominal;
			gui::SetItemTooltip("The camera as it was meant: its specification's nominal model, as planned");
			gui::SameLine();
			if (gui::RadioButton("Realised", m_choice.model == LookModel::Realised))
				m_choice.model = LookModel::Realised;
			gui::SetItemTooltip("The camera a capture renders: the plan's draws, at its seed and scales");
			gui::SameLine();
			gui::BeginDisabled(!m_choice.camera);
			gui::Checkbox("Pin", &m_choice.pinned);
			gui::EndDisabled();
			gui::SetItemTooltip("Stay on this camera while other things are selected and moved");
			gui::SameLine();
			if (m_look && m_choice.camera && !m_look->name.empty())
			{
				std::string readout = string::format("{} · 1/{}", m_look->name, m_divisor);
				if (m_picture.valid())
					readout += string::format(" · {}x{} · {:.1f} ms", m_pictureSize.width, m_pictureSize.height, m_captureMilliseconds);
				gui::TextDisabled("%s", readout.c_str());
			}

			// The picture's area, in the framebuffer's pixels, which is what the step is chosen to fit. What
			// is wrong is drawn over it rather than beside it, so a message coming or going never changes
			// the step, which would build another ray table and lose the message again.
			const ImGuiIO& io = gui::GetIO();
			const ImVec2 origin = gui::GetCursorScreenPos();
			const ImVec2 area{std::max(1.0f, gui::GetContentRegionAvail().x), std::max(1.0f, gui::GetContentRegionAvail().y)};
			m_paneWidth = std::uint32_t(std::max(1.0, std::floor(double(area.x) * io.DisplayFramebufferScale.x)));
			m_paneHeight = std::uint32_t(std::max(1.0, std::floor(double(area.y) * io.DisplayFramebufferScale.y)));
			if (m_picture.valid() && m_pictureSize.width > 0 && m_pictureSize.height > 0)
			{
				// Fitted whole and centred, at the camera's own aspect.
				const float scale = std::min(area.x / float(m_pictureSize.width), area.y / float(m_pictureSize.height));
				const ImVec2 size{float(m_pictureSize.width) * scale, float(m_pictureSize.height) * scale};
				const ImVec2 at{origin.x + (area.x - size.x) * 0.5f, origin.y + (area.y - size.y) * 0.5f};
				gui::GetWindowDrawList()->AddImage(m_texture, at, ImVec2(at.x + size.x, at.y + size.y));
			}

			std::vector<std::string> notes;
			if (!m_choice.camera)
				notes.push_back("Select a camera to look through it.");
			else if (m_look && !m_look->ready())
			{
				if (m_choice.model == LookModel::Realised)
					notes.push_back("This plan has no realisation, so no capture of it could be rendered:");
				notes.insert(notes.end(), m_look->problems.begin(), m_look->problems.end());
			}
			else
			{
				notes.insert(notes.end(), m_stepProblems.begin(), m_stepProblems.end());
				for (const std::string& problem : m_tableProblems)
					notes.push_back("its ray table cannot be built: " + problem);
				if (m_building)
					notes.push_back(string::format("building its ray table at 1/{}...", m_divisor));
				notes.insert(notes.end(), m_captureProblems.begin(), m_captureProblems.end());
				// While assets load, the scene lacks them by design: one line for that, rather than one an
				// asset. Once nothing is loading, what it still lacks failed, and Issues says why too.
				if (ctx.assets.pending() > 0)
					notes.push_back(string::format("{} asset{} still loading: shown as the plan is so far", ctx.assets.pending(),
												   ctx.assets.pending() == 1 ? "" : "s"));
				else
					notes.insert(notes.end(), m_sceneProblems.begin(), m_sceneProblems.end());
				if (ctx.document.plan.lights.empty())
					notes.push_back("This plan has no lights, so the camera sees black: a capture holds only the plan's own light.");
			}
			gui::SetCursorScreenPos(origin);
			gui::PushTextWrapPos(origin.x + area.x);
			for (const std::string& note : notes)
				gui::TextColored(kWarning, "%s", note.c_str());
			gui::PopTextWrapPos();
			// The picture's extent, as an item, so the window's content is the area it was given.
			gui::SetCursorScreenPos(origin);
			gui::Dummy(area);
		}
		gui::End();
	}

	void CameraViewPane::collectTable()
	{
		if (!m_building || !m_building->complete())
			return;
		render::RayTableResult built;
		try
		{
			m_building->get(&built);
		}
		catch (const std::exception& e)
		{
			built.problems = {e.what()};
		}
		m_building.reset();
		std::vector<std::string> problems = std::move(built.problems);
		if (built.table)
		{
			render::GpuRayTableResult uploaded = m_renderer.upload(*built.table);
			if (uploaded.table)
			{
				// The last one is in no capture: a capture waits for the device before it returns.
				m_table = std::move(uploaded.table);
				m_tableModel = m_buildingModel;
				++m_tableVersion;
			}
			problems = std::move(uploaded.problems);
		}
		m_tableProblems = std::move(problems);
		// A model whose table was refused is not asked for again until the model changes.
		m_refusedModel = m_tableProblems.empty() ? std::nullopt : m_buildingModel;
	}

	void CameraViewPane::update(AppContext& ctx, bool gesture)
	{
		const rp::RigPlan& plan = ctx.document.plan;
		follow(m_choice, plan, ctx.selection);
		collectTable();
		if (!m_problems.empty() || !m_visible)
			return;
		if (!m_choice.camera)
		{
			m_picture = {};
			m_look.reset();
			return;
		}

		const PlanAssets& shared = ctx.assets;
		CaptureKey key;
		key.camera = *m_choice.camera;
		key.model = m_choice.model;
		key.plan = shared.planRevision();
		// What it looks through, again when the camera, the model or the plan changed. Another camera or
		// model lets go of the picture at once, rather than show one camera's under another's name.
		if (!m_look || key.camera != m_lookFor.camera || key.model != m_lookFor.model || key.plan != m_lookFor.plan)
		{
			if (!m_look || key.camera != m_lookFor.camera || key.model != m_lookFor.model)
				m_picture = {};
			m_look = lookThrough(plan, key.camera, key.model);
			m_lookFor = key;
			m_atStep.reset();
		}
		if (!m_look->ready())
		{
			m_picture = {};
			return;
		}

		// The step the pane fits, and the model at it.
		const std::uint32_t divisor = lookDivisor(m_look->model->image(), m_paneWidth, m_paneHeight);
		if (!m_atStep || divisor != m_divisor)
		{
			m_divisor = divisor;
			camera::ModelResult at = modelAtDivisor(*m_look->model, divisor);
			m_stepProblems.clear();
			for (const camera::ModelDiagnostic& d : at.diagnostics)
				m_stepProblems.push_back(string::format("{} has no model at 1/{}: {}", m_look->name, divisor, d.detail));
			m_atStep = std::move(at.model);
		}
		if (!m_atStep)
			return;
		key.divisor = divisor;

		// Its ray table, on the pool: posted when the model at this step has none and is not being built,
		// never during a gesture (a lens dragged would build a table for every focal length it passed), and
		// not again for a model whose table was refused. Until it lands, the last picture stays.
		const bool fits = m_tableModel && sameModel(*m_tableModel, *m_atStep);
		const bool refused = m_refusedModel && sameModel(*m_refusedModel, *m_atStep);
		if (!fits && !refused && !m_building && !gesture)
		{
			m_buildingModel = *m_atStep;
			const render::CaptureSettings settings = settingsAtDivisor(render::CaptureSettings{}, divisor);
			m_building.emplace(task::async([model = *m_atStep, settings]()
										   { return render::RayTable::build(model, settings); }));
		}
		if (!fits)
			return;
		key.table = m_tableVersion;

		// Its scene on the device, built again when the camera, the model, the plan, the assets or the
		// frame changed: an update uploads only what the device does not hold already.
		key.assets = shared.assetsRevision();
		key.frame = ctx.frame;
		CaptureKey sceneKey = key;
		sceneKey.divisor = 0;
		sceneKey.table = 0;
		if (!m_gpu || sceneKey != m_sceneFor)
		{
			rp::SceneResult scene = lookThroughScene(plan, *m_look, shared.assets(), ctx.frame);
			m_sceneProblems = std::move(scene.problems);
			std::vector<std::string> device;
			if (!m_gpu)
			{
				render::UploadResult uploaded = m_renderer.upload(scene.scene);
				device = std::move(uploaded.problems);
				if (uploaded.scene)
					m_gpu.emplace(std::move(*uploaded.scene));
			}
			else
				device = m_renderer.update(*m_gpu, scene.scene).problems;
			for (const std::string& problem : device)
				m_sceneProblems.push_back("the camera view could not draw the plan: " + problem);
			m_sceneFor = sceneKey;
		}
		if (!m_gpu || (m_captured && *m_captured == key))
			return;
		capture(key);
	}

	void CameraViewPane::capture(const CaptureKey& key)
	{
		const auto start = std::chrono::steady_clock::now();
		// Not tried again until something it shows changes, whether or not it works.
		m_captured = key;
		m_captureProblems.clear();
		render::RenderResult light = m_renderer.capture(*m_gpu, *m_table, captureView(*m_look));
		for (const std::string& problem : light.problems)
			m_captureProblems.push_back("the capture failed: " + problem);
		if (!light.problems.empty())
			return;
		const image::Image shown = displayed(*m_look->recorder, light.image);
		acm::Texture texture;
		if (shown.valid())
			texture = m_device.createTexture(acm::Format::R8G8B8A8_Unorm, acm::Extent2D{std::uint32_t(shown.width()), std::uint32_t(shown.height())});
		if (!texture.valid())
		{
			m_captureProblems.push_back("the picture could not be shown");
			return;
		}
		texture.upload(shown.data(), shown.byteSize()); // synchronous: shader-read when it returns
		m_picture = std::move(texture);
		m_pictureSize = {std::uint32_t(shown.width()), std::uint32_t(shown.height())};
		m_captureMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	void CameraViewPane::release()
	{
		// The table being built is the pane's to keep, so it is waited for; the pool may still be running.
		if (m_building)
		{
			task::waitAll(*m_building);
			m_building.reset();
		}
		m_texture = {};
		m_picture = {};
		m_table.reset();
		m_gpu.reset();
	}
} // namespace rigplan
