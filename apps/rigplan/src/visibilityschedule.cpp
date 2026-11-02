#include "visibilityschedule.h"

#include <lain/string/format.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	VisibilitySchedule::VisibilitySchedule(std::size_t heldCameras, std::uint32_t faceSize)
		: m_heldCameras(std::max<std::size_t>(heldCameras, 1))
		, m_faceSize(faceSize)
	{
	}

	VisibilitySchedule::~VisibilitySchedule()
	{
		wait();
	}

	const rp::VisibilityMap& VisibilitySchedule::map() const
	{
		static const rp::VisibilityMap none;
		return m_shown ? m_shown->map : m_making ? m_making->map
												 : none;
	}

	const std::vector<rp::VisibilityCamera>& VisibilitySchedule::cameras() const
	{
		static const std::vector<rp::VisibilityCamera> none;
		return m_shown ? m_shown->request.cameras : m_making ? m_making->request.cameras
															 : none;
	}

	const std::vector<std::string>& VisibilitySchedule::problems() const
	{
		static const std::vector<std::string> none;
		return m_making ? m_making->problems : m_shown ? m_shown->problems
													   : none;
	}

	std::uint64_t VisibilitySchedule::revision() const
	{
		return m_making ? m_making->revision : m_shown ? m_shown->revision
													   : 0;
	}

	bool VisibilitySchedule::outOfDate() const
	{
		return m_wanted != 0 && (!m_shown || m_shown->revision != m_wanted);
	}

	VisibilitySchedule::Progress VisibilitySchedule::progress() const
	{
		if (!m_making)
			return {};
		return {m_making->counted, m_making->cameras.size(), m_making->facesDrawn};
	}

	void VisibilitySchedule::want(std::uint64_t revision, bool gesture, bool ready, const std::function<Request()>& request)
	{
		m_wanted = revision;
		if (m_making && m_making->revision == revision)
		{
			m_waiting = false;
			return;
		}
		// Back to the map shown, as an undo of the edit that started another does: nothing to make, and
		// the one under way for that edit is abandoned.
		if (m_shown && m_shown->revision == revision)
		{
			abandon();
			m_waiting = false;
			return;
		}
		if (gesture)
			return;
		m_waiting = !ready;
		if (!ready)
			return;
		start(revision, request());
	}

	void VisibilitySchedule::abandon()
	{
		if (!m_making)
			return;
		// A count cannot be stopped once posted, so it runs to its end and is dropped.
		for (Camera& camera : m_making->cameras)
		{
			if (camera.count)
				m_abandoned.push_back(std::move(*camera.count));
		}
		m_making.reset();
		if (!m_shown)
			++m_shownVersion; // what was shown was the map abandoned
	}

	void VisibilitySchedule::start(std::uint64_t revision, Request request)
	{
		abandon();
		Made made;
		made.revision = revision;
		made.map = rp::VisibilityMap(request.grid);
		made.cameras.resize(request.cameras.size());
		made.request = std::move(request);
		m_making = std::move(made);
		if (!m_shown)
			++m_shownVersion;
		poll(); // a plan with no camera is complete at once
	}

	void VisibilitySchedule::startCamera(Made& made, std::size_t c) const
	{
		Camera& camera = made.cameras[c];
		camera.stage = Stage::Drawing;
		camera.faces = rp::visibilityFaces(made.request.cameras[c].model, m_faceSize);
		camera.drawn.resize(camera.faces.size());
		camera.delivered.assign(camera.faces.size(), false);
	}

	std::optional<VisibilityFaceJob> VisibilitySchedule::next()
	{
		if (!m_making)
			return std::nullopt;
		Made& made = *m_making;
		std::size_t held = 0;
		for (const Camera& camera : made.cameras)
			held += camera.stage == Stage::Drawing || camera.stage == Stage::Counting ? 1 : 0;

		for (std::size_t c = 0; c < made.cameras.size(); ++c)
		{
			Camera& camera = made.cameras[c];
			if (camera.stage == Stage::Waiting)
			{
				if (held >= m_heldCameras)
					return std::nullopt;
				startCamera(made, c);
				++held;
			}
			if (camera.stage != Stage::Drawing || camera.handedOut == camera.faces.size())
				continue;
			VisibilityFaceJob job;
			job.revision = made.revision;
			job.camera = c;
			job.face = camera.handedOut++;
			job.capture = camera.faces[job.face];
			job.view = rp::faceView(job.capture, made.request.cameras[c].planFromCamera);
			return job;
		}
		return std::nullopt;
	}

	void VisibilitySchedule::deliver(const VisibilityFaceJob& job, render::DistanceResult drawn)
	{
		if (!m_making || job.revision != m_making->revision || job.camera >= m_making->cameras.size())
			return;
		Made& made = *m_making;
		Camera& camera = made.cameras[job.camera];
		if (camera.stage != Stage::Drawing || job.face >= camera.delivered.size() || camera.delivered[job.face])
			return;
		camera.delivered[job.face] = true;
		++made.facesDrawn;
		for (const std::string& problem : drawn.problems)
			made.problems.push_back(string::format("camera '{}': a face of its map could not be drawn: {}", made.request.cameras[job.camera].name, problem));
		camera.drawn[job.face] = {camera.faces[job.face], std::move(drawn.image)};
		if (!std::all_of(camera.delivered.begin(), camera.delivered.end(), [](bool d)
						 { return d; }))
			return;

		// Every face back: counted on the pool. The faces go with the count, so they are let go of with it.
		camera.stage = Stage::Counting;
		camera.count.emplace(task::async([grid = made.request.grid, counted = made.request.cameras[job.camera], faces = std::move(camera.drawn)]()
										 { return rp::seenBy(grid, counted, faces); }));
		camera.drawn.clear();
	}

	void VisibilitySchedule::poll()
	{
		m_abandoned.remove_if([](const auto& h)
							  { return h.complete(); });
		if (!m_making)
			return;
		Made& made = *m_making;
		for (std::size_t c = 0; c < made.cameras.size(); ++c)
		{
			Camera& camera = made.cameras[c];
			if (camera.stage != Stage::Counting || !camera.count || !camera.count->complete())
				continue;
			std::vector<bool> seen;
			try
			{
				camera.count->get(&seen);
			}
			catch (const std::exception& e)
			{
				made.problems.push_back(string::format("camera '{}': its map could not be counted: {}", made.request.cameras[c].name, e.what()));
			}
			if (seen.size() != made.request.grid.cells())
				seen.assign(made.request.grid.cells(), false);
			made.map.add(made.request.cameras[c].id, std::move(seen));
			camera.count.reset();
			camera.stage = Stage::Counted;
			++made.counted;
			if (!m_shown)
				++m_shownVersion; // the map shown is this one, filling in
		}
		if (made.counted == made.cameras.size())
		{
			m_shown = std::move(made);
			m_making.reset();
			++m_shownVersion;
		}
	}

	void VisibilitySchedule::wait()
	{
		for (const auto& handle : m_abandoned)
			task::waitAll(handle);
		m_abandoned.clear();
		if (!m_making)
			return;
		for (Camera& camera : m_making->cameras)
		{
			if (camera.count)
				task::waitAll(*camera.count);
		}
	}
} // namespace rigplan
