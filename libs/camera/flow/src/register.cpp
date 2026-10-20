#include "lain/camera/flow/register.h"

#include "lain/camera/flow/boardspecificationnode.h"
#include "lain/camera/flow/calibratecameranode.h"
#include "lain/camera/flow/calibratecameratargetlessnode.h"
#include "lain/camera/flow/calibrationattemptsnode.h"
#include "lain/camera/flow/calibrationmethodnode.h"
#include "lain/camera/flow/cameramodelnode.h"
#include "lain/camera/flow/detectboardnode.h"
#include "lain/camera/flow/nextattemptnode.h"
#include "lain/camera/flow/registercamerasnode.h"
#include "lain/camera/flow/registercamerastargetlessnode.h"
#include "lain/camera/flow/registeredrignode.h"
#include "lain/camera/flow/renderboardnode.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/attempts.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/refinement/refiner.h>
#include <lain/camera/registration/report.h>
#include <lain/flow/porttyperegistry.h>

namespace lain::camera
{
	// What each kind needs to run. A specification is only worth making for something that uses one;
	// board calibration detects and then estimates; and a model only ever comes out of a calibration,
	// of either method.
	static bool canUseBoard()
	{
		return board::canRender() || board::canDetect();
	}

	static bool canCalibrate()
	{
		return board::canDetect() && calibration::canEstimate();
	}

	// Targetless calibration extracts and matches features, poses frames from them, and adjusts the
	// model: whether the refiner can estimate the model asked for is the method's to say, with the
	// reason, as registration's refiner is asked only whether it exists.
	static bool canCalibrateTargetless()
	{
		return feature::canExtract() && feature::canMatch() && feature::canSolveGeometry() && refinement::canRefine();
	}

	// Either calibration: what a model comes out of, and what a fallback chooses between.
	static bool canUseCalibration()
	{
		return canCalibrate() || canCalibrateTargetless();
	}

	// Registration detects, poses each board, and refines the rig: three backends.
	static bool canRegister()
	{
		return board::canDetect() && board::canSolvePose() && refinement::canRefine();
	}

	// Targetless registration extracts and matches features, poses cameras from them, and refines the
	// rig: four backends, and no board's.
	static bool canRegisterTargetless()
	{
		return feature::canExtract() && feature::canMatch() && feature::canSolveGeometry() && refinement::canRefine();
	}

	// Either registration: what a rig comes out of.
	static bool canUseRegistration()
	{
		return canRegister() || canRegisterTargetless();
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
		{kCalibrateCameraTargetlessKey, &canCalibrateTargetless, &add<CalibrateCameraTargetlessNode>},
		{kCalibrationMethodKey, &canUseCalibration, &add<CalibrationMethodNode>},
		{kNextAttemptKey, &canUseCalibration, &add<NextAttemptNode>},
		{kCalibrationAttemptsKey, &canUseCalibration, &add<CalibrationAttemptsNode>},
		{kCameraModelKey, &canUseCalibration, &add<CameraModelNode>},
		{kRegisterCamerasKey, &canRegister, &add<RegisterCamerasNode>},
		{kRegisterCamerasTargetlessKey, &canRegisterTargetless, &add<RegisterCamerasTargetlessNode>},
		{kRegisteredRigKey, &canUseRegistration, &add<RegisteredRigNode>},
	};

	std::vector<std::string> cameraNodeKeys()
	{
		std::vector<std::string> keys;
		for (const Kind& kind : kKinds)
			keys.push_back(kind.key);
		return keys;
	}

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
		flow::registerPortType<calibration::Method>("CalibrationMethod");
		flow::registerPortType<calibration::Attempts>("CalibrationAttempts");
		flow::registerPortType<CameraModel>("CameraModel");
		flow::registerPortType<std::vector<CameraModel>>("ListOfCameraModel");
		flow::registerPortType<registration::Report>("RegistrationReport");
		flow::registerPortType<registration::RegisteredRig>("RegisteredRig");
	}
} // namespace lain::camera
