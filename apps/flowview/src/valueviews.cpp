#include "valueviews.h"

#include "imageview.h"

#include <lain/image/image.h>

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

	void registerBuiltinValueViews(ValueViews& views)
	{
		views.add(typeid(image::Image), &posterOfImage, []
				  { return std::make_unique<ImageView>(); });
	}
} // namespace flowview
