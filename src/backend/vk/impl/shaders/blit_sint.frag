#version 450

layout(push_constant) uniform GPUBlitParams {
  vec4 srcRect;
  vec4 dstRect;
  vec4 invSrcSize;
} params;

layout(set = 0, binding = 0) uniform itexture2D sourceTexture;
layout(set = 0, binding = 1) uniform sampler sourceSampler;

layout(location = 0) out ivec4 color;

void main() {
  vec2 relative = (gl_FragCoord.xy - params.dstRect.xy) *
                  params.dstRect.zw;
  ivec2 coord = ivec2(params.srcRect.xy +
                      relative * params.srcRect.zw);
  color = texelFetch(isampler2D(sourceTexture, sourceSampler), coord, 0);
}
