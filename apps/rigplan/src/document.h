#pragma once

#include <lain/data/value.h>
#include <lain/editor/undo.h>
#include <lain/rigplan/rigplan.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// The rig plan open in the editor, where it lives, whether it has unsaved changes, and its history.
	//
	// ADR-0027 is applied where the plan meets a FILE, and nowhere else: readPlan reads a document's
	// paths against its own folder and saveDocument writes them relative to it, while an undo snapshot is
	// the plan as it is held, every path absolute, since an undo meets no file. So an undo needs no idea
	// where the document lives, and moving one between folders rebases nothing in its history.
	struct Document
	{
		lain::rigplan::RigPlan plan;
		std::filesystem::path path; // absolute; empty for a plan never saved
		bool dirty = false;			// changed since it was opened, made or saved: what the unsaved-changes guard asks
		// Asked for by markChanged and recorded by commit, so an edit made over many frames (a drag) is one
		// step of history.
		bool pendingSnapshot = false;
		lain::editor::UndoStack history;

		// The plan changed: it has unsaved work now, and wants a snapshot once the gesture ends.
		void markChanged()
		{
			dirty = true;
			pendingSnapshot = true;
		}
	};

	// What a New plan starts as: the default Room (8 x 6 x 3 m, a flat grey) and one specification of
	// each kind, so a camera can be placed at once: a 1280 x 720 sensor of 2 um pixels, a 4 mm lens, the
	// camera they make, and a spot light. No mounts.
	lain::rigplan::RigPlan newPlan();

	// A new, untitled document holding newPlan(), its history started at it.
	Document newDocument();

	// The outcome of reading a rig plan document: the plan, or every reason there is none, each naming
	// the file.
	struct PlanRead
	{
		std::optional<lain::rigplan::RigPlan> plan;
		std::vector<std::string> problems; // empty exactly when `plan` is set
	};

	// The rig plan document at `path`, its paths read against its own folder (ADR-0027): the ONE way a
	// plan is read from a file, by `rigplan render` and the editor alike. Needs the data codecs
	// registered.
	PlanRead readPlan(const std::filesystem::path& path);

	// The document at `path`, opened: its plan, its absolute path, nothing unsaved, and a history started
	// at it. None, with every reason, when it does not read.
	struct DocumentRead
	{
		std::optional<Document> document;
		std::vector<std::string> problems;
	};
	DocumentRead openDocument(const std::filesystem::path& path);

	// Writes `document`'s plan to `path` as a rig plan document, its paths relative to the folder `path`
	// is in where they are inside it (ADR-0027), so a plan written where it was read writes the same
	// bytes. On success the document lives at `path` and has nothing unsaved. Returns why it could not be
	// written, or empty; a failed write changes nothing about the document.
	std::vector<std::string> saveDocument(Document& document, const std::filesystem::path& path);

	// The plan as an undo snapshot: rigPlanToValue, its paths as held. Never rebased against a folder.
	lain::data::Value snapshot(const lain::rigplan::RigPlan& plan);

	// Records the document's snapshot as a step of history when one was asked for (markChanged): the
	// caller's call to make once a gesture has ended. A change that came back to where it started records
	// no step (UndoStack::push).
	void commit(Document& document);

	// Steps the plan back or forward through its history, leaving it with unsaved changes. The selection
	// is the caller's, by identity, so it survives a step; one whose object the step removed no longer
	// exists() in the plan. Returns false, changing nothing, when there is no step to take.
	bool undo(Document& document);
	bool redo(Document& document);
} // namespace rigplan
