#pragma once

#include "../appcontext.h"

#include <filesystem>

namespace rigplan
{
	// Which catalogue a save out writes to.
	enum class CatalogueTarget
	{
		Existing, // one that is there, added to: picked with an open panel
		New,	  // one made: named with a save panel; a file already there is refused, left as it was
	};

	// The Specifications menu (CONTEXT.md, "Specification catalogue"):
	//   Copy In from Catalogue...  a catalogue picked, then a specification of it in a picker by kind,
	//                              which says what copying it would bring;
	//   Save to Catalogue...       the selected specification added to a catalogue that is there;
	//   Save to New Catalogue...   a catalogue made of it.
	// Saving into one that has a different specification of the name asks Replace, Keep both or Cancel;
	// a refusal is a transient message. Two save items, not one: a save panel asks to REPLACE a file that
	// is there (every platform's, as lain::gui::saveFile opens it), and a catalogue is added to, never
	// replaced, so a file there is reached through an open panel instead.
	// Stateless: the picker's catalogue and a save waiting on its answer are the AppContext's, as the
	// guard's request is.
	struct SpecificationsMenu
	{
		// The menu, inside the main menu bar: MenuBarPane draws it between Add and View.
		void draw(AppContext& ctx);

		// The picker and the clash question, LATE in the frame, as the unsaved-changes question is. Escape
		// is Cancel in both.
		void drawModals(AppContext& ctx);

		// What the menu's items and the modals' buttons do once a native panel has picked the file, so a
		// test can drive them without one.

		// Reads the catalogue at `file` and opens the picker on it, the first specification of the first kind
		// it has picked. One that does not read says why, and opens nothing. Whether the picker opened.
		bool beginCopyIn(AppContext& ctx, const std::filesystem::path& file);

		// Copies in what the picker has picked (copyIn, edits.h), says what arrived, and closes the picker.
		// One step of history. A specification the plan has already is selected, and nothing changes.
		void copyPicked(AppContext& ctx);

		// Saves the selected specification out to the catalogue at `file`: saved, or a clash held for the
		// question, or why not. A New target is given a .json extension, as Save As gives a plan.
		void saveSelected(AppContext& ctx, const std::filesystem::path& file, CatalogueTarget target);

		// Answers the clash a save out is waiting on, and closes the question.
		void answerClash(AppContext& ctx, ClashAnswer answer);
	};
} // namespace rigplan
