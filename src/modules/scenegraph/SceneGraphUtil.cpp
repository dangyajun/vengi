/**
 * @file
 */

#include "SceneGraphUtil.h"
#include "core/Log.h"
#include <glm/ext/scalar_constants.hpp>
#include "core/collection/DynamicArray.h"
#include "math/Easing.h"
#include "voxel/RawVolume.h"
#include "scenegraph/SceneGraphNode.h"
#include "voxelutil/VolumeCropper.h"
#include "voxelutil/VolumeSplitter.h"

namespace scenegraph {

static int addToGraph(SceneGraph &sceneGraph, SceneGraphNode &&node, int parent) {
	if (parent > 0 && !sceneGraph.hasNode(parent)) {
		parent = sceneGraph.root().id();
	}
	int newNodeId = sceneGraph.emplace(core::move(node), parent);
	if (newNodeId == InvalidNodeId) {
		Log::error("Failed to add node to the scene");
		return InvalidNodeId;
	}
	return newNodeId;
}

static void copy(const SceneGraphNode &node, SceneGraphNode &target, bool copyKeyFrames = true) {
	target.setName(node.name());
	if (copyKeyFrames) {
		target.setKeyFrames(node.keyFrames());
	}
	target.setVisible(node.visible());
	target.setLocked(node.locked());
	target.setPivot(node.pivot());
	target.setColor(node.color());
	target.addProperties(node.properties());
	// TODO: the reference node id might have changed - fix this
	target.setReference(node.reference());
	if (node.type() == SceneGraphNodeType::Model) {
		target.setPalette(node.palette());
		core_assert(node.volume() != nullptr);
	} else if (node.type() == SceneGraphNodeType::ModelReference) {
		core_assert(node.reference() != InvalidNodeId);
	} else {
		core_assert(node.volume() == nullptr);
	}
}

int createNodeReference(SceneGraph &sceneGraph, const SceneGraphNode &node, int parent) {
	if (!node.isReferenceable()) {
		return InvalidNodeId;
	}

	SceneGraphNode newNode(SceneGraphNodeType::ModelReference);
	newNode.setReference(node.id());
	newNode.setName(node.name() + " reference");
	newNode.setColor(node.color());
	newNode.setKeyFrames(node.keyFrames());
	const int mainNodeId = addToGraph(sceneGraph, core::move(newNode), parent < 0 ? node.parent() : parent);
	if (mainNodeId == InvalidNodeId) {
		Log::error("Failed to add node to the scene graph");
		return InvalidNodeId;
	}
	for (int child : node.children()) {
		const SceneGraphNode &childNode = sceneGraph.node(child);
		if (childNode.isReferenceable()) {
			createNodeReference(sceneGraph, childNode, mainNodeId);
		} else {
#if 1
			Log::warn("Don't add node %i - it is not referenceable", child);
#else
			SceneGraphNode newNode(childNode.type());
			copy(childNode, newNode);
			addToGraph(sceneGraph, core::move(newNode), mainNodeId);
#endif
		}
	}
	return mainNodeId;
}

void copyNode(const SceneGraphNode &src, SceneGraphNode &target, bool copyVolume, bool copyKeyFrames) {
	if (copyVolume) {
		core_assert_msg(src.volume() != nullptr, "Source node has no volume - and is of type %d", (int)src.type());
		target.setVolume(new voxel::RawVolume(src.volume()), true);
	} else {
		target.setVolume(src.volume(), false);
	}
	copy(src, target, copyKeyFrames);
}

int addNodeToSceneGraph(SceneGraph &sceneGraph, const SceneGraphNode &node, int parent, bool recursive) {
	SceneGraphNode newNode(node.type());
	copy(node, newNode);
	if (newNode.type() == SceneGraphNodeType::Model) {
		newNode.setVolume(new voxel::RawVolume(node.volume()), true);
	}
	const int nodeId = addToGraph(sceneGraph, core::move(newNode), parent);
	if (recursive) {
		for (int childId : node.children()) {
			const SceneGraphNode &cnode = sceneGraph.node(childId);
			addNodeToSceneGraph(sceneGraph, cnode, nodeId, recursive);
		}
	}
	return nodeId;
}

// TODO: happens to easily to call the wrong function with the const node
// see https://github.com/vengi-voxel/vengi/issues/418
int addNodeToSceneGraph(SceneGraph &sceneGraph, SceneGraphNode &node, int parent, bool recursive) {
	SceneGraphNode newNode(node.type());
	copy(node, newNode);
	if (newNode.type() == SceneGraphNodeType::Model) {
		core_assert(node.owns());
		newNode.setVolume(node.volume(), true);
		node.releaseOwnership();
	}
	const int nodeId = addToGraph(sceneGraph, core::move(newNode), parent);
	if (recursive) {
		for (int childId : node.children()) {
			addNodeToSceneGraph(sceneGraph, sceneGraph.node(childId), nodeId, recursive);
		}
	}
	return nodeId;
}

static int addSceneGraphNode_r(SceneGraph &target, const SceneGraph &source, SceneGraphNode &sourceNode, int parent) {
	const int newNodeId = addNodeToSceneGraph(target, sourceNode, parent);
	if (newNodeId == InvalidNodeId) {
		Log::error("Failed to add node to the scene graph");
		return 0;
	}

	int nodesAdded = sourceNode.type() == SceneGraphNodeType::Model ? 1 : 0;
	for (int sourceNodeIdx : sourceNode.children()) {
		core_assert(source.hasNode(sourceNodeIdx));
		SceneGraphNode &sourceChildNode = source.node(sourceNodeIdx);
		nodesAdded += addSceneGraphNode_r(target, source, sourceChildNode, newNodeId);
	}

	return nodesAdded;
}

int addSceneGraphNodes(SceneGraph &target, SceneGraph &source, int parent) {
	const SceneGraphNode &sourceRoot = source.root();
	int nodesAdded = 0;
	target.node(parent).addProperties(sourceRoot.properties());
	for (int sourceNodeId : sourceRoot.children()) {
		nodesAdded += addSceneGraphNode_r(target, source, source.node(sourceNodeId), parent);
	}
	return nodesAdded;
}

/**
 * @return the main node id that was added
 */
static int copySceneGraphNode_r(SceneGraph &target, const SceneGraph &source, const SceneGraphNode &sourceNode, int parent) {
	SceneGraphNode newNode(sourceNode.type());
	copy(sourceNode, newNode);
	if (newNode.type() == SceneGraphNodeType::Model) {
		newNode.setVolume(new voxel::RawVolume(sourceNode.volume()), true);
	}
	const int newNodeId = addToGraph(target, core::move(newNode), parent);
	if (newNodeId == InvalidNodeId) {
		Log::error("Failed to add node to the scene graph");
		return InvalidNodeId;
	}

	for (int sourceNodeIdx : sourceNode.children()) {
		core_assert(source.hasNode(sourceNodeIdx));
		SceneGraphNode &sourceChildNode = source.node(sourceNodeIdx);
		copySceneGraphNode_r(target, source, sourceChildNode, newNodeId);
	}

	return newNodeId;
}

core::DynamicArray<int> copySceneGraph(SceneGraph &target, const SceneGraph &source, int parent) {
	const SceneGraphNode &sourceRoot = source.root();
	core::DynamicArray<int> nodesAdded;
	target.node(parent).addProperties(sourceRoot.properties());
	for (int sourceNodeId : sourceRoot.children()) {
		nodesAdded.push_back(copySceneGraphNode_r(target, source, source.node(sourceNodeId), parent));
	}

	for (int nodeId : nodesAdded) {
		SceneGraphNode &node = target.node(nodeId);
		if (node.type() == SceneGraphNodeType::ModelReference) {
			// this is not enough of course - the id might have already existed in the target scene graph
			if (!target.hasNode(node.reference())) {
				Log::warn("Reference node %i is not in the scene graph", node.reference());
			}
		}
	}

	// TODO: fix references - see copy() above

	return nodesAdded;
}

// TODO: split is destroying groups
// TODO: for referenced nodes we should have to create new model references for each newly splitted model node, too
bool splitVolumes(const scenegraph::SceneGraph &srcSceneGraph, scenegraph::SceneGraph &destSceneGraph, bool crop,
				  bool createEmpty, bool skipHidden, const glm::ivec3 &maxSize) {
	core_assert(&srcSceneGraph != &destSceneGraph);
	destSceneGraph.reserve(srcSceneGraph.size());
	for (auto iter = srcSceneGraph.beginModel(); iter != srcSceneGraph.end(); ++iter) {
		const scenegraph::SceneGraphNode &node = *iter;
		if (skipHidden && !node.visible()) {
			continue;
		}
		const voxel::Region &region = node.region();
		if (!region.isValid()) {
			Log::warn("invalid region for node %i", node.id());
			continue;
		}
		if (glm::all(glm::lessThanEqual(region.getDimensionsInVoxels(), maxSize))) {
			scenegraph::SceneGraphNode newNode;
			copyNode(node, newNode, true);
			destSceneGraph.emplace(core::move(newNode));
			Log::debug("No split needed for node '%s'", node.name().c_str());
			continue;
		}
		Log::debug("Split needed for node '%s'", node.name().c_str());
		core::DynamicArray<voxel::RawVolume *> rawVolumes;
		voxelutil::splitVolume(node.volume(), maxSize, rawVolumes, createEmpty);
		Log::debug("Created %i volumes", (int)rawVolumes.size());
		for (voxel::RawVolume *v : rawVolumes) {
			scenegraph::SceneGraphNode newNode(SceneGraphNodeType::Model);
			if (crop) {
				voxel::RawVolume *cv = voxelutil::cropVolume(v);
				delete v;
				v = cv;
			}
			copyNode(node, newNode, false);
			newNode.setVolume(v, true);
			destSceneGraph.emplace(core::move(newNode));
		}
	}
	return !destSceneGraph.empty();
}

double interpolate(InterpolationType interpolationType, double current, double start, double end) {
	if (glm::abs(start - end) < glm::epsilon<double>()) {
		return start;
	}
	double deltaFrameSeconds = 0.0f;
	switch (interpolationType) {
	case InterpolationType::Instant:
		deltaFrameSeconds = util::easing::full(current, start, end);
		break;
	case InterpolationType::Linear:
		deltaFrameSeconds = util::easing::linear(current, start, end);
		break;
	case InterpolationType::QuadEaseIn:
		deltaFrameSeconds = util::easing::quadIn(current, start, end);
		break;
	case InterpolationType::QuadEaseOut:
		deltaFrameSeconds = util::easing::quadOut(current, start, end);
		break;
	case InterpolationType::QuadEaseInOut:
		deltaFrameSeconds = util::easing::quadInOut(current, start, end);
		break;
	case InterpolationType::CubicEaseIn:
		deltaFrameSeconds = util::easing::cubicIn(current, start, end);
		break;
	case InterpolationType::CubicEaseOut:
		deltaFrameSeconds = util::easing::cubicOut(current, start, end);
		break;
	case InterpolationType::CubicEaseInOut:
		deltaFrameSeconds = util::easing::cubicInOut(current, start, end);
		break;
	case InterpolationType::Max:
		break;
	}
	return deltaFrameSeconds;
}


} // namespace voxel
