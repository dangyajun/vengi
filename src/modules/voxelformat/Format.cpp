/**
 * @file
 */

#include "Format.h"
#include "core/Var.h"
#include "core/collection/DynamicArray.h"
#include "voxel/CubicSurfaceExtractor.h"
#include "voxel/IsQuadNeeded.h"
#include "voxel/MaterialColor.h"
#include "VolumeFormat.h"
#include "core/Common.h"
#include "core/Log.h"
#include "core/Color.h"
#include "math/Math.h"
#include "voxel/Mesh.h"
#include "voxelformat/SceneGraph.h"
#include "voxelutil/VolumeSplitter.h"
#include "voxelutil/VoxelUtil.h"
#include <limits>

namespace voxel {

const glm::vec4& Format::getColor(const Voxel& voxel) const {
	const voxel::MaterialColorArray& materialColors = voxel::getMaterialColors();
	return materialColors[voxel.getColor()];
}

uint8_t Format::convertPaletteIndex(uint32_t paletteIndex) const {
	if (paletteIndex >= _paletteSize) {
		if (_paletteSize > 0) {
			return paletteIndex % _paletteSize;
		}
		return paletteIndex % _palette.size();
	}
	return _palette[paletteIndex];
}

glm::vec4 Format::findClosestMatch(const glm::vec4& color) const {
	const int index = findClosestIndex(color);
	voxel::MaterialColorArray materialColors = voxel::getMaterialColors();
	return materialColors[index];
}

uint8_t Format::findClosestIndex(const glm::vec4& color) const {
	const voxel::MaterialColorArray& materialColors = voxel::getMaterialColors();
	//materialColors.erase(materialColors.begin());
	return core::Color::getClosestMatch(color, materialColors);
}

void Format::splitVolumes(const SceneGraph& srcSceneGraph, SceneGraph& destSceneGraph, const glm::ivec3 &maxSize) {
	destSceneGraph.reserve(srcSceneGraph.size());
	for (SceneGraphNode &node : srcSceneGraph) {
		const voxel::Region& region = node.region();
		if (glm::all(glm::lessThan(region.getDimensionsInVoxels(), maxSize))) {
			SceneGraphNode newNode;
			newNode.setVolume(new voxel::RawVolume(node.volume()), true);
			newNode.setName(node.name());
			newNode.setVisible(node.visible());
			newNode.setPivot(node.pivot());
			destSceneGraph.emplace_back(core::move(newNode));
			continue;
		}
		core::DynamicArray<voxel::RawVolume *> rawVolumes;
		voxel::splitVolume(node.volume(), maxSize, rawVolumes);
		for (voxel::RawVolume *v : rawVolumes) {
			SceneGraphNode newNode;
			newNode.setVolume(v, true);
			destSceneGraph.emplace_back(core::move(newNode));
		}
	}
}

bool Format::isEmptyBlock(const voxel::RawVolume *v, const glm::ivec3 &maxSize, int x, int y, int z) const {
	const voxel::Region region(x, y, z, x + maxSize.x - 1, y + maxSize.y - 1, z + maxSize.z - 1);
	return voxelutil::isEmpty(*v, region);
}

void Format::calcMinsMaxs(const voxel::Region& region, const glm::ivec3 &maxSize, glm::ivec3 &mins, glm::ivec3 &maxs) const {
	const glm::ivec3 &lower = region.getLowerCorner();
	mins[0] = lower[0] & ~(maxSize.x - 1);
	mins[1] = lower[1] & ~(maxSize.y - 1);
	mins[2] = lower[2] & ~(maxSize.z - 1);

	const glm::ivec3 &upper = region.getUpperCorner();
	maxs[0] = (upper[0] & ~(maxSize.x - 1)) + maxSize.x - 1;
	maxs[1] = (upper[1] & ~(maxSize.y - 1)) + maxSize.y - 1;
	maxs[2] = (upper[2] & ~(maxSize.z - 1)) + maxSize.z - 1;

	Log::debug("%s", region.toString().c_str());
	Log::debug("mins(%i:%i:%i)", mins.x, mins.y, mins.z);
	Log::debug("maxs(%i:%i:%i)", maxs.x, maxs.y, maxs.z);
}

RawVolume* Format::merge(const SceneGraph& sceneGraph) const {
	return sceneGraph.merge();
}

RawVolume* Format::load(const core::String &filename, io::SeekableReadStream& file) {
	ScopedSceneGraph sceneGraph;
	if (!loadGroups(filename, file, sceneGraph)) {
		return nullptr;
	}
	RawVolume* mergedVolume = merge(sceneGraph);
	return mergedVolume;
}

size_t Format::loadPalette(const core::String &filename, io::SeekableReadStream& file, core::Array<uint32_t, 256> &palette) {
	ScopedSceneGraph sceneGraph;
	loadGroups(filename, file, sceneGraph);
	palette = _colors;
	return _colorsSize;
}

image::ImagePtr Format::loadScreenshot(const core::String &filename, io::SeekableReadStream &) {
	Log::debug("%s doesn't have a supported embedded screenshot", filename.c_str());
	return image::ImagePtr();
}

bool Format::save(const RawVolume* volume, const core::String &filename, io::SeekableWriteStream& stream) {
	if (volume == nullptr) {
		return false;
	}
	ScopedSceneGraph sceneGraph;
	SceneGraphNode node;
	node.setVolume(volume, false);
	sceneGraph.emplace_back(core::move(node));
	return saveGroups(sceneGraph, filename, stream);
}

}
