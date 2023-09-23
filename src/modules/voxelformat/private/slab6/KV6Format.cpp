/**
 * @file
 */

#include "KV6Format.h"
#include "SLABShared.h"
#include "core/Color.h"
#include "core/Common.h"
#include "core/Enum.h"
#include "core/FourCC.h"
#include "core/Log.h"
#include "core/RGBA.h"
#include "core/ScopedPtr.h"
#include "core/StringUtil.h"
#include "core/collection/DynamicArray.h"
#include "io/Stream.h"
#include "scenegraph/SceneGraph.h"
#include "voxel/Face.h"
#include "voxel/Palette.h"
#include "voxel/PaletteLookup.h"
#include "voxel/RawVolume.h"
#include "voxelutil/VolumeVisitor.h"
#include <glm/common.hpp>

namespace voxelformat {

namespace priv {

constexpr uint32_t MAXVOXS = 1048576;

struct VoxtypeKV6 {
	uint8_t z = 0;	 ///< z coordinate of this surface voxel (height - our y)
	uint8_t col = 0; ///< palette index
	SLABVisibility vis =
		SLABVisibility::None; ///< Low 6 bits say if neighbor is solid or air - @sa priv::SLABVisibility
	uint8_t dir = 0;		  ///< Uses 256-entry lookup table - lighting bit - @sa priv::directions
};

struct State {
	VoxtypeKV6 voxdata[MAXVOXS];
	int32_t xlen[256]{};
	uint16_t xyoffset[256][256]{};
};

static uint8_t calculateDir(const voxel::RawVolume *, int, int, int, const voxel::Voxel &) {
	return 255u; // TODO
}

} // namespace priv

#define wrap(read)                                                                                                     \
	if ((read) != 0) {                                                                                                 \
		Log::error("Could not load kv6 file: Not enough data in stream " CORE_STRINGIFY(read));                        \
		return 0;                                                                                                      \
	}

size_t KV6Format::loadPalette(const core::String &filename, io::SeekableReadStream &stream, voxel::Palette &palette,
							  const LoadContext &ctx) {
	uint32_t magic;
	wrap(stream.readUInt32(magic))
	if (magic != FourCC('K', 'v', 'x', 'l')) {
		Log::error("Invalid magic");
		return 0;
	}

	uint32_t xsiz_w, ysiz_d, zsiz_h;
	wrap(stream.readUInt32(xsiz_w))
	wrap(stream.readUInt32(ysiz_d))
	wrap(stream.readUInt32(zsiz_h))
	glm::vec3 pivot;
	wrap(stream.readFloat(pivot.x))
	wrap(stream.readFloat(pivot.y))
	wrap(stream.readFloat(pivot.z))

	uint32_t numvoxs;
	wrap(stream.readUInt32(numvoxs))

	const int64_t headerSize = 32;
	const int64_t xLenSize = (int64_t)(xsiz_w * sizeof(uint32_t));
	const int64_t yLenSize = (int64_t)((size_t)xsiz_w * (size_t)ysiz_d * sizeof(uint16_t));
	const int64_t paletteOffset = headerSize + (int64_t)(numvoxs * 8) + xLenSize + yLenSize;
	if (stream.seek(paletteOffset) != -1) {
		if (stream.remaining() != 0) {
			uint32_t palMagic;
			wrap(stream.readUInt32(palMagic))
			if (palMagic == FourCC('S', 'P', 'a', 'l')) {
				palette.setSize(voxel::PaletteMaxColors);
				for (int i = 0; i < voxel::PaletteMaxColors; ++i) {
					uint8_t r, g, b;
					wrap(stream.readUInt8(b))
					wrap(stream.readUInt8(g))
					wrap(stream.readUInt8(r))
					palette.color(i) = core::RGBA(r, g, b, 255u);
				}
			}
			return palette.size();
		}
	}
	return 0;
}

#undef wrap
#define wrap(read)                                                                                                     \
	if ((read) != 0) {                                                                                                 \
		Log::error("Could not load kv6 file: Not enough data in stream " CORE_STRINGIFY(read));                        \
		return false;                                                                                                  \
	}

bool KV6Format::loadGroupsPalette(const core::String &filename, io::SeekableReadStream &stream,
								  scenegraph::SceneGraph &sceneGraph, voxel::Palette &palette, const LoadContext &ctx) {
	uint32_t magic;
	wrap(stream.readUInt32(magic))
	if (magic != FourCC('K', 'v', 'x', 'l')) {
		Log::error("Invalid magic");
		return false;
	}

	// Dimensions of voxel. (our depth is kv6 height)
	uint32_t xsiz_w, ysiz_d, zsiz_h;
	wrap(stream.readUInt32(xsiz_w))
	wrap(stream.readUInt32(ysiz_d))
	wrap(stream.readUInt32(zsiz_h))

	if (xsiz_w > 256 || ysiz_d > 256 || zsiz_h > 255) {
		Log::error("Dimensions exceeded: w: %i, h: %i, d: %i", xsiz_w, zsiz_h, ysiz_d);
		return false;
	}

	scenegraph::SceneGraphTransform transform;
	glm::vec3 pivot;
	wrap(stream.readFloat(pivot.x))
	wrap(stream.readFloat(pivot.y))
	wrap(stream.readFloat(pivot.z))

	pivot.z = (float)zsiz_h - 1.0f - pivot.z;

	glm::vec3 normalizedPivot = pivot / glm::vec3(xsiz_w, ysiz_d, zsiz_h);
	core::exchange(normalizedPivot.y, normalizedPivot.z);

	const voxel::Region region(0, 0, 0, (int)xsiz_w - 1, (int)zsiz_h - 1, (int)ysiz_d - 1);
	if (!region.isValid()) {
		Log::error("Invalid region: %i:%i:%i", xsiz_w, zsiz_h, ysiz_d);
		return false;
	}

	uint32_t numvoxs;
	wrap(stream.readUInt32(numvoxs))
	Log::debug("numvoxs: %u", numvoxs);
	if (numvoxs > priv::MAXVOXS) {
		Log::error("Max allowed voxels exceeded: %u (max is %u)", numvoxs, priv::MAXVOXS);
		return false;
	}

	const int64_t headerSize = 32;
	const int64_t xLenSize = (int64_t)(xsiz_w * sizeof(uint32_t));
	const int64_t yLenSize = (int64_t)((size_t)xsiz_w * (size_t)ysiz_d * sizeof(uint16_t));
	const int64_t paletteOffset = headerSize + (int64_t)numvoxs * (int64_t)8 + xLenSize + yLenSize;
	if (stream.seek(paletteOffset) != -1) {
		if (stream.remaining() != 0) {
			uint32_t palMagic;
			wrap(stream.readUInt32(palMagic))
			if (palMagic == FourCC('S', 'P', 'a', 'l')) {
				palette.setSize(voxel::PaletteMaxColors);
				for (int i = 0; i < voxel::PaletteMaxColors; ++i) {
					uint8_t r, g, b;
					wrap(stream.readUInt8(b))
					wrap(stream.readUInt8(g))
					wrap(stream.readUInt8(r))
					palette.color(i) = core::RGBA(r, g, b, 255u);
				}
			}
		}
	}
	stream.seek(headerSize);

	core::ScopedPtr<priv::State> state(new priv::State());
	voxel::PaletteLookup palLookup(palette);
	for (uint32_t c = 0u; c < numvoxs; ++c) {
		uint8_t palr, palg, palb, pala;
		wrap(stream.readUInt8(palb))
		wrap(stream.readUInt8(palg))
		wrap(stream.readUInt8(palr))
		wrap(stream.readUInt8(pala)) // always 128
		const glm::vec4 &color = core::Color::fromRGBA(palr, palg, palb, 255);
		state->voxdata[c].col = palLookup.findClosestIndex(color);
		wrap(stream.readUInt8(state->voxdata[c].z))
		uint8_t zhigh;
		wrap(stream.readUInt8(zhigh))
		wrap(stream.readUInt8((uint8_t &)state->voxdata[c].vis))
		wrap(stream.readUInt8(state->voxdata[c].dir))
		Log::debug("voxel %u/%u z-low: %u, vis: %i. dir: %u, pal: %u", c, numvoxs, state->voxdata[c].z,
				   (uint8_t)state->voxdata[c].vis, state->voxdata[c].dir, state->voxdata[c].col);
	}
	for (uint32_t x = 0u; x < xsiz_w; ++x) {
		wrap(stream.readInt32(state->xlen[x]))
		Log::debug("xlen[%u]: %i", x, state->xlen[x]);
	}

	for (uint32_t x = 0u; x < xsiz_w; ++x) {
		for (uint32_t y = 0u; y < ysiz_d; ++y) {
			wrap(stream.readUInt16(state->xyoffset[x][y]))
			Log::debug("xyoffset[%u][%u]: %u", x, y, state->xyoffset[x][y]);
		}
	}

	voxel::RawVolume *volume = new voxel::RawVolume(region);

	int idx = 0;
	for (uint32_t x = 0; x < xsiz_w; ++x) {
		for (uint32_t y = 0; y < ysiz_d; ++y) {
			for (int end = idx + state->xyoffset[x][y]; idx < end; ++idx) {
				const priv::VoxtypeKV6 &vox = state->voxdata[idx];
				const voxel::Voxel col = voxel::createVoxel(palette, vox.col);
				volume->setVoxel((int)x, (int)((zsiz_h - 1) - vox.z), (int)y, col);
			}
		}
	}

	idx = 0;
	for (uint32_t x = 0; x < xsiz_w; ++x) {
		for (uint32_t y = 0; y < ysiz_d; ++y) {
			voxel::Voxel lastCol;
			uint32_t lastZ = 256;
			for (int end = idx + state->xyoffset[x][y]; idx < end; ++idx) {
				const priv::VoxtypeKV6 &vox = state->voxdata[idx];
				if ((vox.vis & priv::SLABVisibility::Up) != priv::SLABVisibility::None) {
					lastZ = vox.z;
					lastCol = voxel::createVoxel(palette, vox.col);
				}
				if ((vox.vis & priv::SLABVisibility::Down) != priv::SLABVisibility::None) {
					for (; lastZ < vox.z; ++lastZ) {
						volume->setVoxel((int)x, (int)((zsiz_h - 1) - lastZ), (int)y, lastCol);
					}
				}
			}
		}
	}

	scenegraph::SceneGraphNode node;
	node.setVolume(volume, true);
	node.setName(filename);
	scenegraph::KeyFrameIndex keyFrameIdx = 0;
	node.setPivot(normalizedPivot);
	node.setTransform(keyFrameIdx, transform);
	node.setPalette(palLookup.palette());
	sceneGraph.emplace(core::move(node));

	return true;
}

#undef wrap

#define wrapBool(read)                                                                                                 \
	if ((read) == false) {                                                                                             \
		Log::error("Could not write kv6 file: Not enough space in stream " CORE_STRINGIFY(read));                      \
		return false;                                                                                                  \
	}

bool KV6Format::saveGroups(const scenegraph::SceneGraph &sceneGraph, const core::String &filename,
						   io::SeekableWriteStream &stream, const SaveContext &ctx) {
	const scenegraph::SceneGraphNode *node = sceneGraph.firstModelNode();
	core_assert(node);

	const voxel::Region &region = node->region();
	const glm::ivec3 &dim = region.getDimensionsInVoxels();

	if (dim.x > 256 || dim.z > 256 || dim.y > 255) {
		Log::error("Dimensions exceeded: w: %i, h: %i, d: %i", dim.x, dim.y, dim.z);
		return false;
	}

	int32_t xoffsets[256]{};
	uint16_t xyoffsets[256][256]{}; // our z

	core::DynamicArray<priv::VoxtypeKV6> voxdata;
	const uint32_t numvoxs = voxelutil::visitSurfaceVolume(
		*node->volume(),
		[&](int x, int y, int z, const voxel::Voxel &voxel) {
			priv::VoxtypeKV6 vd;
			const int shiftedX = x - region.getLowerX();
			// flip y and z here
			const int shiftedZ = z - region.getLowerZ();
			vd.z = region.getHeightInCells() - (y - region.getLowerY());
			vd.col = voxel.getColor();
			vd.vis = priv::calculateVisibility(node->volume(), x, y, z);
			vd.dir = priv::calculateDir(node->volume(), x, y, z, voxel);
			voxdata.push_back(vd);
			++xoffsets[shiftedX];
			++xyoffsets[shiftedX][shiftedZ];
		},
		voxelutil::VisitorOrder::XZY);

	constexpr uint32_t MAXVOXS = 1048576;
	if (numvoxs > MAXVOXS) {
		Log::error("Max allowed voxels exceeded: %u (max is %u)", numvoxs, MAXVOXS);
		return false;
	}

	wrapBool(stream.writeUInt32(FourCC('K', 'v', 'x', 'l')))

	const int xsiz_w = dim.x;
	// flip y and z here
	const int ysiz_d = dim.z;
	const int zsiz_h = dim.y;
	wrapBool(stream.writeUInt32(xsiz_w))
	wrapBool(stream.writeUInt32(ysiz_d))
	wrapBool(stream.writeUInt32(zsiz_h))

	glm::vec3 pivot(0.0f);
	wrapBool(stream.writeFloat(-pivot.x))
	wrapBool(stream.writeFloat(pivot.z))
	wrapBool(stream.writeFloat(-pivot.y))

	wrapBool(stream.writeUInt32(numvoxs))

	for (const priv::VoxtypeKV6 &data : voxdata) {
		const core::RGBA color = node->palette().color(data.col);
		wrapBool(stream.writeUInt8(color.b))
		wrapBool(stream.writeUInt8(color.g))
		wrapBool(stream.writeUInt8(color.r))
		wrapBool(stream.writeUInt8(128))
		wrapBool(stream.writeUInt8(data.z))
		wrapBool(stream.writeUInt8(0))
		wrapBool(stream.writeUInt8((uint8_t)data.vis))
		wrapBool(stream.writeUInt8(data.dir))
		Log::debug("voxel z-low: %u, vis: %i. dir: %u, pal: %u", data.z, (uint8_t)data.vis, data.dir, data.col);
	}

	for (int x = 0u; x < xsiz_w; ++x) {
		wrapBool(stream.writeInt32(xoffsets[x]))
		Log::debug("xlen[%u]: %i", x, xoffsets[x]);
	}

	for (int x = 0; x < xsiz_w; ++x) {
		for (int y = ysiz_d - 1; y >= 0; --y) {
			wrapBool(stream.writeUInt16(xyoffsets[x][y]))
			Log::debug("xyoffset[%u][%u]: %u", x, y, xyoffsets[x][y]);
		}
	}

	const uint32_t palMagic = FourCC('S', 'P', 'a', 'l');
	wrapBool(stream.writeUInt32(palMagic))
	for (int i = 0; i < node->palette().colorCount(); ++i) {
		const core::RGBA color = node->palette().color(i);
		wrapBool(stream.writeUInt8(color.b))
		wrapBool(stream.writeUInt8(color.g))
		wrapBool(stream.writeUInt8(color.r))
	}
	for (int i = node->palette().colorCount(); i < voxel::PaletteMaxColors; ++i) {
		wrapBool(stream.writeUInt8(0))
		wrapBool(stream.writeUInt8(0))
		wrapBool(stream.writeUInt8(0))
	}

	return true;
}

#undef wrapBool

} // namespace voxelformat
