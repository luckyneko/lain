// flowview-examples: writes apps/flowview/examples. Run through the build (`examples` rewrites the
// documents, `examples-data` also the data sets), or directly:
//
//     flowview-examples <examples-folder> [--data]
//
// The documents are what the test holds the committed ones to, so after changing a builder, rerun
// this and commit what it wrote.
//
//     flowview-examples --camera-footage <folder>
//
// draws the camera examples' footage into <folder> instead (the `camera-footage` target writes it into
// the build folder), with a README.txt of the commands that run each example on it. It is never
// committed (camerafootage.h).

#include "camerafootage.h"
#include "examples.h"
#include "graphio.h" // registerSceneSerialization
#include "scene.h"	 // registerExampleNodes: the production palette, so flowview opens what this writes

#include <lain/camera/backends.h>
#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/io/data/codecs.h>
#include <lain/io/image/codecs.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/log/log.h>
#include <lain/task/task.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

// The camera examples' footage into `folder`, drawn on the process pool.
static int writeCameraFootage(const std::filesystem::path& folder)
{
	lain::io::image::registerImageCodecs();
	lain::io::data::registerDataCodecs(); // a rig's camera models are camera-model documents
	lain::camera::registerCameraBackends();
	const bool started = lain::task::start(-1);
	int status = EXIT_SUCCESS;
	try
	{
		flowview::examples::footage::writeAll(folder);
		lain::log::info("flowview-examples: drew the camera footage in {}", folder.string());
	}
	catch (const std::exception& error)
	{
		lain::log::error("flowview-examples: {}", error.what());
		status = EXIT_FAILURE;
	}
	if (started)
		lain::task::stop();
	return status;
}

int main(int argc, char** argv)
{
	std::filesystem::path folder;
	bool data = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--camera-footage")
		{
			if (i + 1 >= argc || argc != 3)
			{
				lain::log::error("usage: flowview-examples --camera-footage <folder>");
				return EXIT_FAILURE;
			}
			return writeCameraFootage(argv[i + 1]);
		}
		if (arg == "--data")
			data = true;
		else if (folder.empty())
			folder = arg;
		else
		{
			lain::log::error("flowview-examples: unexpected argument '{}'", arg);
			return EXIT_FAILURE;
		}
	}
	if (folder.empty())
	{
		lain::log::error("usage: flowview-examples <examples-folder> [--data] | --camera-footage <folder>");
		return EXIT_FAILURE;
	}

	lain::io::image::registerImageCodecs();
	lain::io::video::registerVideoCodecs(); // shot.mp4 is written only when a video writer registered
	lain::io::sequence::registerSequenceOpeners();
	flowview::registerSceneSerialization();
	lain::core::Factory<lain::flow::Node> factory;
	flowview::registerExampleNodes(factory, 64);

	try
	{
		if (data)
			flowview::examples::writeData(folder);
		flowview::examples::writeDocuments(folder, factory);
	}
	catch (const std::exception& error)
	{
		lain::log::error("flowview-examples: {}", error.what());
		return EXIT_FAILURE;
	}
	lain::log::info("flowview-examples: wrote {}", folder.string());
	return EXIT_SUCCESS;
}
