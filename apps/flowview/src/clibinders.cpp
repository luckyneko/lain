#include "clibinders.h"

#include <lain/core/parse.h>
#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/sequence/open.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace flowview
{
	using namespace lain;

	void BoundaryBinders::add(std::type_index type, Binder binder)
	{
		m_binders[type] = std::move(binder);
	}

	bool BoundaryBinders::has(std::type_index type) const
	{
		return m_binders.count(type) != 0;
	}

	std::optional<flow::PortValue> BoundaryBinders::bind(std::type_index type, const std::string& value) const
	{
		const auto it = m_binders.find(type);
		if (it == m_binders.end())
			return std::nullopt;
		return it->second(value);
	}

	// --- built-in parsers (each: string -> optional<PortValue>) --------------------------

	static std::optional<flow::PortValue> bindInt(const std::string& s)
	{
		const std::optional<int> v = core::parse<int>(s);
		if (!v.has_value())
			return std::nullopt; // not a number, out of range, or trailing junk ("12x")
		flow::PortValue pv;
		pv.set<int>(*v);
		return pv;
	}

	static std::optional<flow::PortValue> bindFloat(const std::string& s)
	{
		const std::optional<float> v = core::parse<float>(s);
		if (!v.has_value())
			return std::nullopt;
		flow::PortValue pv;
		pv.set<float>(*v);
		return pv;
	}

	static std::optional<flow::PortValue> bindBool(const std::string& s)
	{
		std::string t;
		t.reserve(s.size());
		for (const char c : s)
			t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		flow::PortValue pv;
		if (t == "true" || t == "1")
		{
			pv.set<bool>(true);
			return pv;
		}
		if (t == "false" || t == "0")
		{
			pv.set<bool>(false);
			return pv;
		}
		return std::nullopt;
	}

	static std::optional<flow::PortValue> bindString(const std::string& s)
	{
		flow::PortValue pv;
		pv.set<std::string>(s);
		return pv;
	}

	static std::optional<flow::PortValue> bindImage(const std::string& s)
	{
		auto image = io::image::load(s);
		if (!image)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<image::Image>(std::move(*image));
		return pv;
	}

	// A frame sequence binds by OPENING the uri, the way an image binds by loading one — a folder
	// of stills, a "shot.<frame:04>.png" pattern, or a video file, dispatched by
	// io::sequence::open so the cli names no medium.
	static std::optional<flow::PortValue> bindFrameSequence(const std::string& s)
	{
		auto sequence = io::sequence::open(s);
		if (!sequence)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<media::FrameSequence>(std::move(*sequence));
		return pv;
	}

	// A single position, for binding one frame without a sweep. The RANGE form (`0-499`) is the
	// sweep's business and is parsed by framerange.h: a binder answers "what value is this string",
	// and a range is not one value.
	static std::optional<flow::PortValue> bindFramePosition(const std::string& s)
	{
		// core::parse REFUSES a negative here, where std::stoull accepted one and wrapped it: "-12"
		// used to bind position 18446744073709551604, a wrong answer that said nothing about being
		// wrong. A frame position has no negative, so refusing is the only honest reading.
		const std::optional<std::size_t> position = core::parse<std::size_t>(s);
		if (!position.has_value())
			return std::nullopt;

		flow::PortValue pv;
		pv.set<media::FramePosition>(media::FramePosition{*position});
		return pv;
	}

	void registerBoundaryBinders(BoundaryBinders& binders)
	{
		binders.add(typeid(int), &bindInt);
		binders.add(typeid(float), &bindFloat);
		binders.add(typeid(bool), &bindBool);
		binders.add(typeid(std::string), &bindString);
		binders.add(typeid(image::Image), &bindImage);
		binders.add(typeid(media::FrameSequence), &bindFrameSequence);
		binders.add(typeid(media::FramePosition), &bindFramePosition);
	}
} // namespace flowview
