#include "lain/camera/flow/register.h"

#include "lain/camera/flow/boardspecificationnode.h"
#include "lain/camera/flow/calibratecameranode.h"
#include "lain/camera/flow/cameramodelnode.h"
#include "lain/camera/flow/detectboardnode.h"
#include "lain/camera/flow/renderboardnode.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/flow/porttyperegistry.h>

namespace lain::camera
{
	// What each kind needs to run. A specification is only worth making for something that uses one;
	// calibration detects and then estimates; and a model only ever comes out of a calibration.
	static bool canUseBoard()
	{
		return board::canRender() || board::canDetect();
	}

	static bool canCalibrate()
	{
		return board::canDetect() && calibration::canEstimate();
	}

	template <typename T>
	static void add(core::Factory<flow::Node>& factory, const char* key)
	{
		factory.registerType<T>(key);
	}

	// One row per kind, in display order: its key, the capability it needs, and how it registers.
	// availableCameraNodeKeys and registerCameraNodes both read THIS, so a menu cannot offer a kind
	// the factory lacks: the factory has every row, and the menu the rows whose capability is here.
	struct Kind
	{
		const char* key;
		bool (*available)();
		void (*registerInto)(core::Factory<flow::Node>&, const char*);
	};
	static const Kind kKinds[] = {
		{kBoardSpecificationKey, &canUseBoard, &add<BoardSpecificationNode>},
		{kRenderBoardKey, &board::canRender, &add<RenderBoardNode>},
		{kDetectBoardKey, &board::canDetect, &add<DetectBoardNode>},
		{kCalibrateCameraKey, &canCalibrate, &add<CalibrateCameraNode>},
		{kCameraModelKey, &canCalibrate, &add<CameraModelNode>},
	};

	std::vector<std::string> availableCameraNodeKeys()
	{
		std::vector<std::string> keys;
		for (const Kind& kind : kKinds)
		{
			if (kind.available())
				keys.push_back(kind.key);
		}
		return keys;
	}

	void registerCameraNodes(core::Factory<flow::Node>& factory)
	{
		for (const Kind& kind : kKinds)
			kind.registerInto(factory, kind.key);
	}

	void registerCameraPortTypes()
	{
		flow::registerPortType<board::Specification>("BoardSpecification");
		flow::registerPortType<board::DetectionReport>("BoardDetectionReport");
		flow::registerPortType<calibration::Report>("CalibrationReport");
		flow::registerPortType<CameraModel>("CameraModel");
	}
} // namespace lain::camera
