/**
 * @file
 */

#pragma once

#include "MeshExporter.h"

namespace tinygltf {
class Model;
class Node;
struct Scene;
struct Mesh;
struct Material;
struct Primitive;
struct Accessor;
} // namespace tinygltf

namespace voxelformat {

/**
 * @brief GL Transmission Format
 */
class GLTFFormat : public MeshExporter {
private:
	// exporting
	struct Pair {
		constexpr Pair(int f, int s) : first(f), second(s) {
		}
		int first;
		int second;
	};
	typedef core::DynamicArray<Pair> Stack;
	void processGltfNode(tinygltf::Model &m, tinygltf::Node &node, tinygltf::Scene &scene,
						 const SceneGraphNode &graphNode, Stack &stack);

	// importing (voxelization)
	struct GltfVertex {
		glm::vec3 pos;
		core::String texture;
	};
	bool loadGlftAttributes(const core::StringMap<image::ImagePtr> &textures, const tinygltf::Model &model,
							   const tinygltf::Primitive &primitive, core::DynamicArray<GltfVertex> &vertices,
							   core::DynamicArray<glm::vec2> &uvs) const;

	bool loadGltfNode_r(SceneGraph &sceneGraph, core::StringMap<image::ImagePtr> &textures, tinygltf::Model &model, int gltfNodeIdx, int parentNodeId) const;
	bool loadGltfIndices(const tinygltf::Model &model, const tinygltf::Primitive &primitive, core::DynamicArray<uint32_t> &indices) const;
	voxelformat::SceneGraphTransform loadGltfTransform(const tinygltf::Node &gltfNode) const;
	size_t getGltfAccessorSize(const tinygltf::Accessor &accessor) const;
	const tinygltf::Accessor *getGltfAccessor(const tinygltf::Model &model, int id) const;

	bool subdivideShape(const tinygltf::Model &model, const core::DynamicArray<uint32_t> &indices,
						const core::DynamicArray<GltfVertex> &vertices, const core::DynamicArray<glm::vec2> &uvs,
						const core::StringMap<image::ImagePtr> &textures, core::DynamicArray<Tri> &subdivided) const;
	void calculateAABB(const core::DynamicArray<GltfVertex> &vertices, glm::vec3 &mins, glm::vec3 &maxs) const;

public:
	bool saveMeshes(const core::Map<int, int> &meshIdxNodeMap, const SceneGraph &sceneGraph, const Meshes &meshes,
					const core::String &filename, io::SeekableWriteStream &stream, const glm::vec3 &scale, bool quad,
					bool withColor, bool withTexCoords) override;
	bool loadGroups(const core::String &filename, io::SeekableReadStream &stream, SceneGraph &sceneGraph) override;
};

} // namespace voxelformat
