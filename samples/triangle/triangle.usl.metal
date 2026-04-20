#include <metal_stdlib>
using namespace metal;

struct VSIn {
  float2 position [[attribute(0)]];
  float4 color [[attribute(1)]];
};

struct VSOut {
  float4 position [[position]];
  float4 color;
};

vertex VSOut tri_vs(VSIn in [[stage_in]]) {
  VSOut out;
  out.position = float4(in.position, 0.0, 1.0);
  out.color = in.color;
  return out;
}

fragment float4 tri_fs(VSOut in [[stage_in]]) {
  return in.color;
}
