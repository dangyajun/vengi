#if 0
__constant sampler_t volumeSampler =
      CLK_NORMALIZED_COORDS_FALSE
    | CLK_ADDRESS_CLAMP_TO_EDGE
    | CLK_FILTER_NEAREST;
#endif

#include "voxel.cl"

__kernel void extractCubicMesh(__read_only image3d_t volume, sampler_t volumeSampler, __global uchar *output, uint imageWidth, uint imageHeight, uint imageDepth) {
	uint x = get_global_id(0);
	uint y = get_global_id(1);
	uint z = get_global_id(2);

	float u = x / (float) imageWidth;
	float v = y / (float) imageHeight;
	float w = z / (float) imageDepth;

	uint4 voxel = read_imageui(volume, volumeSampler, (float4)(u, v, w, 1.0f));

	if (x < imageWidth && y < imageHeight) {
		uint i = (((imageHeight - 1) - y) * imageWidth * 4) + x * 4;
		output[i + 0] = output[i + 1] = output[i + 2] = 0;
		output[i + 3] = 255;
		if (voxel[0] == Air) {
			output[i + 2] = 255;
		} else if (voxel[0] == Grass) {
			output[i + 1] = 255;
		} else if (voxel[0] == Dirt) {
			output[i + 0] = 127;
			output[i + 1] = 64;
		} else { // unknown 0xff00ffff
			output[i + 0] = 255;
			output[i + 1] = 0;
			output[i + 2] = 255;
		}
	}
}
