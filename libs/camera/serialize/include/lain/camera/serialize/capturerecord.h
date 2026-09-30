#pragma once

#include "lain/camera/serialize/cameramodel.h"

#include <lain/camera/capturerecord.h>
#include <lain/data/data.h>

// Capture records as documents. The imported model is carried as its unchecked parameters, as the
// record holds it; whoever uses it reads it through CameraModel::create.
namespace lain::camera
{
	LAIN_SERIALIZE(DeviceDescription, make, model, identity)
	LAIN_SERIALIZE(StreamDescription, width, height, format, framesPerSecond)
	LAIN_SERIALIZE(ImportedModel, source, parameters)
	LAIN_SERIALIZE(CaptureRecord, device, stream, properties, importedModel)
} // namespace lain::camera
