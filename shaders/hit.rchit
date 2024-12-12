#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
	vec3 pos, normal;
	vec2 uv0, uv1;
};

struct Material {
	vec4 baseColourFactor;
	float roughnessFactor, metallicFactor;
	int baseColourTexIdx, roughnessMetallicTexIdx, normalTexIdx;
	bool doubleSided;
};

struct GeometryInfo {
	uint64_t vertexBufferAddress, indexBufferAddress;
	uint materialIdx;
};

struct HitInfo {
	vec3 pos, normal;
	vec2 uv0, uv1;
};

layout(buffer_reference, scalar) buffer Vertices { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer Indices { uint32_t indices[]; };
layout(binding = 3, set = 0, scalar) readonly buffer GeometryInfos { GeometryInfo geometryInfos[]; };
layout(binding = 4, set = 0, scalar) readonly buffer Materials { Material materials[]; };
layout(binding = 5, set = 0) uniform sampler2D textures[];

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

HitInfo unpackTriangle(uint idx, vec3 weights) {
	GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
	Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
	Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
	
	HitInfo hitInfo;
	hitInfo.pos = vec3(0.0);
	hitInfo.normal = vec3(0.0);
	hitInfo.uv0 = vec2(0.0);
	hitInfo.uv1 = vec2(0.0);
	for (int i = 0; i < 3; i++) {
		uint index = indexBuffer.indices[3 * idx + i];
		Vertex vertex = vertexBuffer.vertices[index];

		hitInfo.pos += vertex.pos * weights[i];
		hitInfo.normal += vertex.normal * weights[i];
		hitInfo.uv0 += vertex.uv0 * weights[i];
		hitInfo.uv1 += vertex.uv1 * weights[i];
	}
	hitInfo.normal = normalize(hitInfo.normal);

	return hitInfo;
}

void main()
{
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
  HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords);

  GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
  Material mat = materials[geometryInfo.materialIdx];

  vec3 baseColour;

  if (mat.baseColourTexIdx != -1)
	  baseColour = texture(textures[nonuniformEXT(mat.baseColourTexIdx)], hitInfo.uv0).xyz;
  else
      baseColour = mat.baseColourFactor.xyz;

  hitValue = baseColour;
  
}