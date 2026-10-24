#pragma once

#include <memory>
#include <optional>
#include <utility>

namespace lain::editor
{
	// The user's answer to "this would throw away unsaved changes".
	enum class UnsavedAnswer
	{
		Save,	 // save the document, then carry the request out
		Discard, // carry the request out and lose the changes
		Cancel,	 // keep the document as it is
	};

	// The rule every destructive request goes through — New, Open, anything that replaces the open
	// document — so unsaved work is never thrown away unasked. A request made with nothing unsaved
	// comes straight back to be carried out; otherwise it is held until the user answers, and only a
	// Save that went through, or a Discard, lets it out. A Save the user cancels (the save panel
	// closed) or that fails cancels the request with it: work the user just asked to keep is never
	// discarded.
	//
	// Driver-free: the app asks its question however it likes (a modal), and decides what makes its
	// document unsaved. `Request` is the app's own description of the swap.
	template <typename Request>
	class DocumentGuard
	{
	public:
		// Ask for a destructive request. With nothing `unsaved` it comes back, to carry out now.
		// Otherwise it is held for the user's answer and nothing comes back. A new request replaces one
		// still held: a question closed without an answer (Escape) leaves its request behind, and the
		// next ask supersedes it.
		std::optional<Request> request(Request request, bool unsaved)
		{
			m_pending.reset();
			m_prompt = false; // a question not yet opened was about the request just replaced
			if (!unsaved)
				return std::optional<Request>(std::move(request));
			m_pending = std::make_unique<Request>(std::move(request));
			m_prompt = true;
			return std::nullopt;
		}

		// True exactly once after a request is held: the app should start asking now. A flag rather
		// than a call into the app, because the question often has to be opened from elsewhere (an
		// ImGui popup opens in the id scope that draws it, not the menu that asked).
		bool takePrompt() { return std::exchange(m_prompt, false); }

		// The request waiting for an answer, or null.
		const Request* pending() const { return m_pending.get(); }

		// The user answered. Save calls `save`, which reports whether the document was saved, and lets
		// the request out only if it was; Discard lets it out without saving; Cancel drops it. Nothing
		// is held afterwards, and with nothing held there is nothing to answer: no save is attempted
		// and nothing comes back.
		template <typename Save>
		std::optional<Request> answer(UnsavedAnswer answer, Save&& save)
		{
			const std::unique_ptr<Request> request = std::move(m_pending);
			m_prompt = false;
			if (!request)
				return std::nullopt;
			switch (answer)
			{
				case UnsavedAnswer::Save:
					if (!std::forward<Save>(save)())
						return std::nullopt;
					return std::move(*request);
				case UnsavedAnswer::Discard:
					return std::move(*request);
				case UnsavedAnswer::Cancel:
					return std::nullopt;
			}
			return std::nullopt;
		}

	private:
		// On the heap, not in an optional: GCC 13 at -O2 calls an optional<std::string> that reset()
		// emptied and that is then refilled maybe-uninitialized, refilled by = or by emplace alike, and
		// -Werror fails the Release build. A pointer has no storage to be unsure of, and pending()
		// answers one anyway. A held request is one per unsaved-changes question, so the allocation
		// costs nothing.
		std::unique_ptr<Request> m_pending;
		bool m_prompt = false;
	};
} // namespace lain::editor
