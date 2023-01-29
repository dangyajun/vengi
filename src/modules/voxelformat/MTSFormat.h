/**
 * @file
 */

#pragma once

#include "Format.h"

namespace voxelformat {

/**
 * The Minetest Schematic File Format (minetest)
 *
 * @note https://dev.minetest.net/Minetest_Schematic_File_Format
 *
 * @ingroup Formats
 */
class MTSFormat : public PaletteFormat {
public:
	bool loadGroupsPalette(const core::String &filename, io::SeekableReadStream &stream, SceneGraph &sceneGraph,
						   voxel::Palette &palette) override;
	bool saveGroups(const SceneGraph &sceneGraph, const core::String &filename, io::SeekableWriteStream &stream,
					ThumbnailCreator thumbnailCreator) override;
};

} // namespace voxelformat