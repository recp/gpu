#version 450

layout(push_constant) uniform GPUBlitParams {
  vec4 srcRect;
  vec4 dstRect;
  vec4 invSrcSize;
} params;

layout(set = 0, binding = 0) uniform texture2D sourceTexture;
layout(set = 0, binding = 1) uniform sampler sourceSampler;

layout(location = 0) out vec4 color;

void main() {
  vec2 relative = (gl_FragCoord.xy - params.dstRect.xy) *
                  params.dstRect.zw;
  vec2 uv = (params.srcRect.xy + relative * params.srcRect.zw) *
            params.invSrcSize.xy;
  color = textureLod(sampler2D(sourceTexture, sourceSampler), uv, 0.0);
}
