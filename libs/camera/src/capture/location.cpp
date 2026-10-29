#include "lain/camera/capture/location.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace lain::camera::capture
{
	using Components = std::vector<std::string>;

	// `text`'s components, split at every '/' and '\'. A leading separator leaves an empty first
	// component, so an absolute path never shares a prefix with a relative one; trailing empty
	// components, a folder spelled with a closing separator, are dropped.
	static Components components(std::string_view text)
	{
		Components out(1);
		for (const char c : text)
		{
			if (c == '/' || c == '\\')
				out.emplace_back();
			else
				out.back() += c;
		}
		while (out.size() > 1 && out.back().empty())
			out.pop_back();
		return out;
	}

	static std::string join(Components::const_iterator begin, Components::const_iterator end)
	{
		std::string out;
		for (auto it = begin; it != end; ++it)
		{
			if (it != begin)
				out += '/';
			out += *it;
		}
		return out;
	}

	// How many leading components `a` and `b` share.
	static std::size_t shared(const Components& a, const Components& b)
	{
		std::size_t n = 0;
		while (n < a.size() && n < b.size() && a[n] == b[n])
			++n;
		return n;
	}

	// A camera's location (footageRoot), or nothing when it has no frame.
	static std::optional<Components> location(const media::FrameSequence& footage)
	{
		if (footage.size() == 0)
			return std::nullopt;
		std::set<std::string> sources;
		for (std::size_t k = 0; k < footage.size(); ++k)
			sources.insert(footage.frame(k).source.toString());
		if (sources.size() == 1)
			return components(*sources.begin());
		std::optional<Components> folder;
		for (const std::string& source : sources)
		{
			Components c = components(source);
			c.pop_back(); // the folder it is in
			if (!folder)
				folder = std::move(c);
			else
				folder->resize(shared(*folder, c));
		}
		return folder;
	}

	static std::vector<std::optional<Components>> locationsOf(const std::vector<media::FrameSequence>& footage)
	{
		std::vector<std::optional<Components>> out;
		for (const media::FrameSequence& f : footage)
			out.push_back(location(f));
		return out;
	}

	// The longest prefix every location extends strictly, so no camera's location is the root itself;
	// nothing with no location.
	static std::optional<Components> rootOf(const std::vector<std::optional<Components>>& locations)
	{
		const Components* first = nullptr;
		std::size_t n = 0;
		for (const std::optional<Components>& l : locations)
		{
			if (!l)
				continue;
			if (!first)
			{
				first = &*l;
				n = l->size();
			}
			n = std::min({n, shared(*first, *l), l->size() - 1});
		}
		if (!first)
			return std::nullopt;
		return Components(first->begin(), first->begin() + std::ptrdiff_t(n));
	}

	// A root as text. The filesystem's own root, a single empty component, is "/" rather than "", which
	// relativeTo would read as no root.
	static std::string rootText(const Components& root)
	{
		if (root.size() == 1 && root.front().empty())
			return "/";
		return join(root.begin(), root.end());
	}

	std::string footageRoot(const std::vector<media::FrameSequence>& footage)
	{
		const std::optional<Components> root = rootOf(locationsOf(footage));
		return root ? rootText(*root) : std::string{};
	}

	std::string relativeTo(const core::Uri& source, std::string_view root)
	{
		const std::string& text = source.toString();
		if (root.empty())
			return text;
		const Components r = components(root);
		const Components s = components(text);
		if (s.size() <= r.size() || shared(r, s) != r.size())
			return text;
		return join(s.begin() + std::ptrdiff_t(r.size()), s.end());
	}

	std::vector<CameraIdentity> identitiesByLocation(const std::vector<media::FrameSequence>& footage)
	{
		const std::vector<std::optional<Components>> locations = locationsOf(footage);
		std::vector<CameraIdentity> out(footage.size());
		const std::optional<Components> root = rootOf(locations);
		if (!root)
			return out;
		std::map<std::string, std::size_t> uses;
		for (std::size_t c = 0; c < footage.size(); ++c)
		{
			if (!locations[c])
				continue;
			out[c].value = join(locations[c]->begin() + std::ptrdiff_t(root->size()), locations[c]->end());
			++uses[out[c].value];
		}
		// Cameras sharing a location are told apart by their first frame instead.
		const std::string text = rootText(*root);
		for (std::size_t c = 0; c < footage.size(); ++c)
		{
			if (locations[c] && uses[out[c].value] > 1)
				out[c].value = relativeTo(footage[c].frame(0).source, text);
		}
		return out;
	}
} // namespace lain::camera::capture
