struct Vertex {
	vec3 pos, normal;
	vec4 tangent;
	vec2 uv0, uv1;
};

struct GeometryInfo {
	uint64_t vertexBufferAddress, indexBufferAddress;
	uint materialIdx;
};

layout(buffer_reference, scalar) buffer Vertices { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer Indices { uint32_t indices[]; };
layout(binding = 5, set = 0, scalar) readonly buffer GeometryInfos { GeometryInfo geometryInfos[]; };