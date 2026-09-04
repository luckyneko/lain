#include "valueviews.h"

#include "imageview.h"
#include "sequenceview.h"

#include <lain/image/image.h>
#include <lain/media/framesequence.h>

#include <typeindex>
#include <utility>

namespace flowview
{
	using namespace lain;

	// --- registry ----------------------------------------------------------------------

	void ValueViews::add(std::type_index type, Poster poster, Maker maker)
	{
		m_views.insert_or_assign(type, Entry{std::move(poster), std::move(maker)});
	}

	flow::PortValue ValueViews::poster(const flow::PortValue& value) const
	{
		if (value.empty())
			return {};
		const auto it = m_views.find(value.type());
		if (it == m_views.end())
			return {};
		return it->second.poster(value);
	}

	std::unique_ptr<ValueView> ValueViews::make(std::type_index type) const
	{
		const auto it = m_views.find(type);
		return it != m_views.end() ? it->second.maker() : nullptr;
	}

	bool ValueViews::has(std::type_index type) const
	{
		return m_views.count(type) != 0;
	}

	// --- built-in views ----------------------------------------------------------------

	// An image IS its own poster, so this ALIASES rather than copying: the returned slot shares the
	// source's allocation through shared_ptr's aliasing constructor (ADR-0014's mechanism, reused).
	// A copy here would be a deep pixel copy per image port per edit.
	static flow::PortValue posterOfImage(const flow::PortValue& value)
	{
		if (!value.holds<image::Image>())
			return {};
		return flow::PortValue::alias(value, value.get<image::Image>());
	}

	// A sequence's poster is its FIRST FRAME, decoded. Unlike an image's it cannot be aliased —
	// there is no image on the port to point at — so this is where the registry earns its return
	// type: producing a poster may be work, and for a type where it is, it happens once per edit
	// rather than once per drawn frame.
	//
	// An EMPTY sequence posters nothing. That is not a failure: a folder that exists and holds no
	// images is a value, and the pin still describes itself as "0 frames" in text.
	static flow::PortValue posterOfSequence(const flow::PortValue& value)
	{
		if (!value.holds<media::FrameSequence>())
			return {};
		const media::FrameSequence& sequence = value.get<media::FrameSequence>();
		if (sequence.empty())
			return {};
		image::Image frame = sequence.image(0);
		if (!frame.valid())
			return {}; // the frame would not decode; the pin gets text and no thumbnail
		flow::PortValue poster;
		poster.set<image::Image>(std::move(frame));
		return poster;
	}

	void registerBuiltinValueViews(ValueViews& views)
	{
		views.add(typeid(image::Image), &posterOfImage, []
				  { return std::make_unique<ImageView>(); });
		views.add(typeid(media::FrameSequence), &posterOfSequence, []
				  { return std::make_unique<SequenceView>(); });
	}
} // namespace flowview
