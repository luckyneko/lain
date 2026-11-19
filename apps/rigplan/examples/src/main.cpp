// rigplan-examples: writes apps/rigplan/examples. Run through the build (`rig-plans` rewrites the
// documents), or directly:
//
//     rigplan-examples <examples-folder>
//
// The documents are what the test holds the committed ones to, so after changing a builder, rerun this
// and commit what it wrote.

#include "examples.h"

#include <lain/io/data/codecs.h>
#include <lain/log/log.h>

#include <cstdlib>
#include <exception>
#include <filesystem>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		lain::log::error("usage: rigplan-examples <examples-folder>");
		return EXIT_FAILURE;
	}
	const std::filesystem::path folder = argv[1];
	lain::io::data::registerDataCodecs();
	try
	{
		rigplan::examples::writeDocuments(folder);
	}
	catch (const std::exception& error)
	{
		lain::log::error("{}", error.what());
		return EXIT_FAILURE;
	}
	lain::log::info("rigplan-examples: wrote {}", folder.string());
	return EXIT_SUCCESS;
}
