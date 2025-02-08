layout(binding = 12, set = 0) uniform sampler2D textures[];

vec4 textureGet(int idx, vec2 texCoord) {
	return texture(textures[nonuniformEXT(idx)], texCoord);
}