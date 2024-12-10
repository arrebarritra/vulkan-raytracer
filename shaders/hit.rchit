#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
  const vec3 barycentricCoords = vec3(float((gl_PrimitiveID >> 0) & 1), float((gl_PrimitiveID >> 1) & 1), float((gl_PrimitiveID >> 2) & 1));
  hitValue = barycentricCoords;
}