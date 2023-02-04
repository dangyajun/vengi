/**
 * @file
 */

#pragma once

#include "command/CommandHandler.h"
#include "core/Var.h"

namespace video {
class Camera;
}
namespace voxelformat {
class SceneGraph;
class SceneGraphNode;
}

namespace voxedit {

struct ModelNodeSettings;

class SceneGraphPanel {
private:
	core::VarPtr _animationSpeedVar;
	core::VarPtr _hideInactive;
	bool _showNodeDetails = true;
	bool _hasFocus = false;

	void recursiveAddNodes(video::Camera &camera, const voxelformat::SceneGraph &sceneGraph,
							  voxelformat::SceneGraphNode &node, command::CommandExecutionListener &listener,
							  int depth) const;

public:
	bool _popupNewModelNode = false;
	bool init();
	void update(video::Camera& camera, const char *title, ModelNodeSettings* layerSettings, command::CommandExecutionListener &listener);
	bool hasFocus() const;
};

}
