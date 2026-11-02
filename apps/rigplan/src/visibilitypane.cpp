#include "visibilitypane.h"

#include "visibilityview.h"

#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/visibility.h>
#include <lain/string/format.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	static const ImVec4 kWarning{0.9f, 0.7f, 0.25f, 1.0f};
	// How long a frame spends drawing faces: a 1024-texel face takes 0.7 ms on Metal and 4.7 ms on the
	// CPU device, so a few a frame on the first and one on the second (WORK.md M15, *Sub-slice 9g built*).
	static constexpr std::chrono::milliseconds kFaceBudget{4};

	VisibilityPane::VisibilityPane(acm::Device& device, gui::Context& gui)
		: m_device(device)
		, m_renderer(device)
	{
		m_problems = m_renderer.problems();
		m_texture = gui.frameTexture([this](std::uint32_t)
									 { return m_picture; });
	}

	VisibilityPane::~VisibilityPane()
	{
		release();
	}

	static std::string plural(std::size_t n, const char* one, const char* many)
	{
		return string::format("{} {}", n, n == 1 ? one : many);
	}

	void VisibilityPane::draw(AppContext& ctx)
	{
		m_visible = false;
		if (gui::Begin("Visibility", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			m_visible = true;
			if (!m_problems.empty())
			{
				gui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f), "The visibility map cannot be drawn on this device:");
				for (const std::string& problem : m_problems)
					gui::TextWrapped("%s", problem.c_str());
				gui::End();
				return;
			}

			// The plane, and how far the map has got.
			if (gui::RadioButton("Floor", !m_atHeight))
				m_atHeight = false;
			gui::SetItemTooltip("Count over the floor");
			gui::SameLine();
			if (gui::RadioButton("Height", m_atHeight))
				m_atHeight = true;
			gui::SetItemTooltip("Count over a plane at this height above the floor: where a board is held, say");
			gui::SameLine();
			gui::BeginDisabled(!m_atHeight);
			gui::SetNextItemWidth(gui::GetFontSize() * 6);
			gui::DragFloat("##height", &m_height, 0.01f, 0.0f, 20.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
			gui::EndDisabled();
			gui::SameLine();
			const rp::VisibilityMap& map = m_schedule.map();
			const rp::VisibilityGrid& grid = map.grid();
			std::string readout;
			if (m_schedule.working())
			{
				const VisibilitySchedule::Progress p = m_schedule.progress();
				readout = string::format("counting {} of {}...", p.camerasCounted, plural(p.cameras, "camera", "cameras"));
			}
			else if (m_schedule.outOfDate())
				readout = m_schedule.waiting() ? "waiting for meshes still loading" : "out of date: made again when the edit ends";
			else if (grid.cells() > 0)
				readout = string::format("{} · {:.1f} x {:.1f} m in {:.0f} cm cells", plural(m_schedule.cameras().size(), "camera", "cameras"),
										 grid.columns * grid.cell, grid.rows * grid.cell, grid.cell * 100);
			gui::TextDisabled("%s", readout.c_str());

			// The picture, fitted whole above the legend, north up.
			const ImGuiIO& io = gui::GetIO();
			const float legendHeight = gui::GetFrameHeightWithSpacing() + gui::GetTextLineHeightWithSpacing();
			const ImVec2 origin = gui::GetCursorScreenPos();
			const ImVec2 area{std::max(1.0f, gui::GetContentRegionAvail().x), std::max(1.0f, gui::GetContentRegionAvail().y - legendHeight)};
			ImDrawList* draw = gui::GetWindowDrawList();
			if (grid.cells() > 0 && m_picture.valid())
			{
				const float scale = std::min(area.x / float(grid.columns), area.y / float(grid.rows));
				const ImVec2 size{float(grid.columns) * scale, float(grid.rows) * scale};
				const ImVec2 at{origin.x + (area.x - size.x) * 0.5f, origin.y + (area.y - size.y) * 0.5f};
				const ImVec2 end{at.x + size.x, at.y + size.y};
				draw->AddImage(m_texture, at, end);
				draw->AddRect(at, end, gui::GetColorU32(ImGuiCol_Border));
				m_scaleWanted = visibilityPictureScale(grid, double(std::max(size.x, size.y)) * io.DisplayFramebufferScale.x);

				// The cell under the pointer: where it is, how many cameras see it, and which.
				if (gui::IsWindowHovered() && io.MousePos.x >= at.x && io.MousePos.y >= at.y && io.MousePos.x < end.x && io.MousePos.y < end.y)
				{
					if (const auto cell = cellUnder(grid, (io.MousePos.x - at.x) / size.x, (io.MousePos.y - at.y) / size.y))
					{
						const auto [column, row] = *cell;
						const math::Vec3d centre = grid.centre(column, row);
						const std::vector<core::Uuid> seeing = map.camerasAt(column, row);
						std::string names;
						for (const rp::VisibilityCamera& camera : m_schedule.cameras()) // in the plan's order
						{
							if (std::find(seeing.begin(), seeing.end(), camera.id) != seeing.end())
								names += (names.empty() ? "" : ", ") + camera.name;
						}
						gui::BeginTooltip();
						gui::Text("x %.2f m, y %.2f m", centre.x, centre.y);
						if (seeing.empty())
							gui::Text("no camera sees it");
						else
							gui::Text("%s: %s", plural(seeing.size(), "camera", "cameras").c_str(), names.c_str());
						gui::EndTooltip();
					}
				}
			}

			// What is wrong, over the picture, so a note coming or going never moves it.
			std::vector<std::string> notes;
			if (ctx.document.plan.cameras.empty())
				notes.push_back("This plan has no cameras.");
			else if (!m_schedule.working() && m_schedule.cameras().empty() && !m_schedule.outOfDate())
				notes.push_back("No camera has a pose and a model to count from: Issues says why.");
			if (!m_scene.problems.empty() && ctx.assets.pending() == 0)
			{
				notes.push_back("Counted without what did not load:");
				notes.insert(notes.end(), m_scene.problems.begin(), m_scene.problems.end());
			}
			notes.insert(notes.end(), m_deviceProblems.begin(), m_deviceProblems.end());
			notes.insert(notes.end(), m_schedule.problems().begin(), m_schedule.problems().end());
			gui::SetCursorScreenPos(origin);
			gui::PushTextWrapPos(origin.x + area.x);
			for (const std::string& note : notes)
				gui::TextColored(kWarning, "%s", note.c_str());
			gui::PopTextWrapPos();
			gui::SetCursorScreenPos(origin);
			gui::Dummy(area);

			// The legend: each colour, and the count it stands for, written.
			const float swatch = gui::GetTextLineHeight();
			for (const LegendEntry& entry : visibilityLegend())
			{
				const ImVec2 p = gui::GetCursorScreenPos();
				draw->AddRectFilled(p, ImVec2(p.x + swatch, p.y + swatch), IM_COL32(entry.colour[0], entry.colour[1], entry.colour[2], 255));
				draw->AddRect(p, ImVec2(p.x + swatch, p.y + swatch), gui::GetColorU32(ImGuiCol_Border));
				gui::Dummy(ImVec2(swatch, swatch));
				gui::SameLine(0.0f, gui::GetStyle().ItemInnerSpacing.x);
				gui::Text("%zu%s", entry.count, entry.orMore ? "+" : "");
				gui::SameLine();
			}
			gui::NewLine();
			gui::TextDisabled("cameras seeing each cell: one alone is still a blind spot, since registration needs shared views");
		}
		gui::End();
	}

	void VisibilityPane::update(AppContext& ctx, bool gesture)
	{
		m_schedule.poll();
		if (!m_problems.empty() || !m_visible)
			return;

		// The revision wanted: a new one when the plan, the plane or the static scene changed. Every asset
		// that lands is looked at, but a bake changes nothing in a static scene, so only a mesh starts again.
		const rp::RigPlan& plan = ctx.document.plan;
		const PlanAssets& shared = ctx.assets;
		const bool plane = m_atHeight != m_atHeightFor || (m_atHeight && m_height != m_heightFor);
		if (m_revision == 0 || shared.planRevision() != m_planFor || shared.assetsRevision() != m_assetsFor || plane)
		{
			rp::SceneResult scene = rp::staticScene(plan, rp::nominalLayout(plan), shared.assets());
			Fingerprint fingerprint;
			for (const render::Instance& instance : scene.scene.instances)
			{
				fingerprint.meshes.push_back(scene.scene.meshes[instance.mesh].get());
				fingerprint.placements.push_back(instance.worldFromMesh);
			}
			if (m_revision == 0 || shared.planRevision() != m_planFor || plane || !(fingerprint == m_fingerprint))
				++m_revision;
			m_scene = std::move(scene);
			m_fingerprint = std::move(fingerprint);
			m_planFor = shared.planRevision();
			m_assetsFor = shared.assetsRevision();
			m_atHeightFor = m_atHeight;
			m_heightFor = m_height;
		}
		const bool ready = m_scene.problems.empty() || shared.pending() == 0;
		const double height = m_atHeight ? double(m_height) : 0.0;
		m_schedule.want(m_revision, gesture, ready,
						[&]
						{ return VisibilitySchedule::Request{rp::visibilityGrid(plan, height), rp::visibilityCameras(rp::nominalLayout(plan))}; });

		// The static scene of the map under way, on the device: an update uploads only what it lacks.
		if (m_schedule.working() && m_uploadedFor != m_schedule.revision())
		{
			m_deviceProblems.clear();
			std::vector<std::string> problems;
			if (!m_gpu)
			{
				render::UploadResult uploaded = m_renderer.upload(m_scene.scene);
				problems = std::move(uploaded.problems);
				if (uploaded.scene)
					m_gpu.emplace(std::move(*uploaded.scene));
			}
			else
				problems = m_renderer.update(*m_gpu, m_scene.scene).problems;
			for (const std::string& problem : problems)
				m_deviceProblems.push_back("the visibility map could not draw the plan: " + problem);
			m_uploadedFor = m_schedule.revision();
		}

		// Its faces, for a few milliseconds.
		if (m_schedule.working() && m_gpu && m_deviceProblems.empty())
		{
			const auto start = std::chrono::steady_clock::now();
			while (std::chrono::steady_clock::now() - start < kFaceBudget)
			{
				std::optional<VisibilityFaceJob> job = m_schedule.next();
				if (!job)
					break;
				m_schedule.deliver(*job, m_renderer.renderDistance(*m_gpu, job->view));
			}
			m_schedule.poll();
		}
		if (m_schedule.shownVersion() != m_pictureFor || m_scaleWanted != m_pictureScale)
			makePicture();
	}

	void VisibilityPane::makePicture()
	{
		m_pictureFor = m_schedule.shownVersion();
		m_pictureScale = m_scaleWanted;
		const image::Image picture = visibilityPicture(m_schedule.map(), m_scaleWanted);
		if (!picture.valid())
		{
			m_picture = {};
			return;
		}
		// A new texture each time, never one rewritten in place: a frame in flight may be drawing the last.
		acm::Texture texture = m_device.createTexture(acm::Format::R8G8B8A8_Unorm, acm::Extent2D{std::uint32_t(picture.width()), std::uint32_t(picture.height())});
		if (!texture.valid())
		{
			m_picture = {};
			return;
		}
		texture.upload(picture.data(), picture.byteSize()); // synchronous: shader-read when it returns
		m_picture = std::move(texture);
	}

	void VisibilityPane::release()
	{
		m_schedule.wait();
		m_texture = {};
		m_picture = {};
		m_gpu.reset();
	}
} // namespace rigplan
