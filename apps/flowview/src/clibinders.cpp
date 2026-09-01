#include "clibinders.h"

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
		try
		{
			std::size_t pos = 0;
			const int v = std::stoi(s, &pos);
			if (pos != s.size())
				return std::nullopt; // trailing junk ("12x") isn't a clean int
			flow::PortValue pv;
			pv.set<int>(v);
			return pv;
		}
		catch (...)
		{
			return std::nullopt; // not a number / out of range
		}
	}

	static std::optional<flow::PortValue> bindFloat(const std::string& s)
	{
		try
		{
			std::size_t pos = 0;
			const float v = std::stof(s, &pos);
			if (pos != s.size())
				return std::nullopt;
			flow::PortValue pv;
			pv.set<float>(v);
			return pv;
		}
		catch (...)
		{
			return std::nullopt;
		}
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
	// of stills, a ####-numbered pattern, or (once slice 5 lands) a video file, dispatched by
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
		std::size_t consumed = 0;
		unsigned long long parsed = 0;
		try
		{
			parsed = std::stoull(s, &consumed);
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}
		if (consumed != s.size()) // trailing junk, as the scalar binders above also refuse
			return std::nullopt;

		flow::PortValue pv;
		pv.set<media::FramePosition>(media::FramePosition{static_cast<std::size_t>(parsed)});
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
