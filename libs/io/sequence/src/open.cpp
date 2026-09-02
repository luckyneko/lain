#include "lain/io/sequence/open.h"

#include <lain/io/uri.h>
#include <lain/log/log.h>

#include <map>
#include <utility>

namespace lain::io::sequence
{
	// The registry. A plain map rather than a core::Factory: a Factory creates polymorphic objects
	// keyed by name, and an opener is a function — there is nothing to construct, and no dynamic
	// type for keyOf() to report.
	struct Registry
	{
		std::map<std::string, Opener> byExtension;
		Opener fallback;
	};

	static Registry& registry()
	{
		static Registry instance;
		return instance;
	}

	void registerOpener(std::string extension, Opener opener)
	{
		registry().byExtension[std::move(extension)] = std::move(opener);
	}

	void registerDefaultOpener(Opener opener)
	{
		registry().fallback = std::move(opener);
	}

	std::optional<lain::media::FrameSequence> open(std::string_view uri, lain::media::FrameRate rate)
	{
		const Registry& registered = registry();

		// io::extensionKey, not a local copy: this picks a MEDIUM by the same string io::image
		// picks a codec by, and a spelling decided twice eventually disagrees with itself.
		const std::string extension = lain::io::extensionKey(uri);

		const auto claimed = registered.byExtension.find(extension);
		if (claimed != registered.byExtension.end())
			return claimed->second(uri, rate);

		if (registered.fallback)
			return registered.fallback(uri, rate);

		lain::log::error("io::sequence: no opener for {} — call io::sequence::registerSequenceOpeners()",
						 std::string(uri));
		return std::nullopt;
	}
} // namespace lain::io::sequence
