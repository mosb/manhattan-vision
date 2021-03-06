#pragma once

#include "matlab_structure.h"
#include "camera.h"

namespace indoor_context {
	// Represents a calibrated frame (similar to PosedImage)
	extern MatlabProto FrameProto;
	// Represents an objective function optimized by ManhattanDP (similar to DPObjective)
	extern MatlabProto ObjectiveProto;
	// Represents a training example
	extern MatlabProto CaseProto;
	// Represents a solution to a reconstruction problem
	extern MatlabProto SolutionProto;
	// Represents meta-information output from dp_load_cases
	extern MatlabProto MetaProto;
	// Make a frame proto into a PosedCamera
	void FrameStructToCamera(const ConstMatlabStructure& frame,
													 LinearCamera& camera,
													 PosedCamera& pc);
}
