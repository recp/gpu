#ifndef gpu_sample_cube_data_h
#define gpu_sample_cube_data_h

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <stdint.h>

typedef struct CubeVertex {
  float position[3];
  float normal[3];
  float uv[2];
} CubeVertex;

typedef struct CubeUniforms {
  mat4 mvp;
  mat4 model;
} CubeUniforms;

enum {
  CUBE_CHECKER_SIZE = 16u,
  CUBE_INDEX_COUNT  = 36u
};

#define CUBE_VERTEX(px, py, pz, nx, ny, nz, u, v) \
  {{px, py, pz}, {nx, ny, nz}, {u, v}}

static const CubeVertex kCubeVertices[] = {
  CUBE_VERTEX(-1, -1,  1,  0,  0,  1, 0, 1),
  CUBE_VERTEX( 1, -1,  1,  0,  0,  1, 1, 1),
  CUBE_VERTEX( 1,  1,  1,  0,  0,  1, 1, 0),
  CUBE_VERTEX(-1,  1,  1,  0,  0,  1, 0, 0),

  CUBE_VERTEX( 1, -1, -1,  0,  0, -1, 0, 1),
  CUBE_VERTEX(-1, -1, -1,  0,  0, -1, 1, 1),
  CUBE_VERTEX(-1,  1, -1,  0,  0, -1, 1, 0),
  CUBE_VERTEX( 1,  1, -1,  0,  0, -1, 0, 0),

  CUBE_VERTEX( 1, -1,  1,  1,  0,  0, 0, 1),
  CUBE_VERTEX( 1, -1, -1,  1,  0,  0, 1, 1),
  CUBE_VERTEX( 1,  1, -1,  1,  0,  0, 1, 0),
  CUBE_VERTEX( 1,  1,  1,  1,  0,  0, 0, 0),

  CUBE_VERTEX(-1, -1, -1, -1,  0,  0, 0, 1),
  CUBE_VERTEX(-1, -1,  1, -1,  0,  0, 1, 1),
  CUBE_VERTEX(-1,  1,  1, -1,  0,  0, 1, 0),
  CUBE_VERTEX(-1,  1, -1, -1,  0,  0, 0, 0),

  CUBE_VERTEX(-1,  1,  1,  0,  1,  0, 0, 1),
  CUBE_VERTEX( 1,  1,  1,  0,  1,  0, 1, 1),
  CUBE_VERTEX( 1,  1, -1,  0,  1,  0, 1, 0),
  CUBE_VERTEX(-1,  1, -1,  0,  1,  0, 0, 0),

  CUBE_VERTEX(-1, -1, -1,  0, -1,  0, 0, 1),
  CUBE_VERTEX( 1, -1, -1,  0, -1,  0, 1, 1),
  CUBE_VERTEX( 1, -1,  1,  0, -1,  0, 1, 0),
  CUBE_VERTEX(-1, -1,  1,  0, -1,  0, 0, 0)
};

#undef CUBE_VERTEX

static const uint16_t kCubeIndices[] = {
   0u,  1u,  2u,  0u,  2u,  3u,
   4u,  5u,  6u,  4u,  6u,  7u,
   8u,  9u, 10u,  8u, 10u, 11u,
  12u, 13u, 14u, 12u, 14u, 15u,
  16u, 17u, 18u, 16u, 18u, 19u,
  20u, 21u, 22u, 20u, 22u, 23u
};

static const uint8_t kCubeCheckerOrange[] = {255u, 122u, 18u, 255u};
static const uint8_t kCubeCheckerBlue[]   = {18u, 154u, 230u, 255u};

_Static_assert(sizeof(CubeUniforms) == 128u,
               "cube uniform layout must match two float4x4 matrices");
_Static_assert(sizeof(CubeVertex) == 32u,
               "cube vertex layout must match the pipeline stride");
_Static_assert(sizeof(kCubeIndices) / sizeof(kCubeIndices[0]) ==
                 CUBE_INDEX_COUNT,
               "cube index count must match the draw call");

static inline void
CubeBuildViewProjection(uint32_t width, uint32_t height, mat4 dest) {
  vec3  eye    = {0.0f, 0.0f, 4.5f};
  vec3  center = {0.0f, 0.0f, 0.0f};
  vec3  up     = {0.0f, 1.0f, 0.0f};
  mat4  view;
  mat4  projection;
  float aspect;

  aspect = height > 0u ? (float)width / (float)height : 1.0f;
  glm_lookat(eye, center, up, view);
  glm_perspective(glm_rad(48.0f), aspect, 0.1f, 100.0f, projection);
  glm_mat4_mul(projection, view, dest);
}

static inline void
CubeBuildUniforms(float seconds,
                  mat4 viewProjection,
                  CubeUniforms *uniforms) {
  vec3 axisX = {1.0f, 0.0f, 0.0f};
  vec3 axisY = {0.0f, 1.0f, 0.0f};

  glm_mat4_identity(uniforms->model);
  glm_rotate(uniforms->model, seconds * 0.72f, axisY);
  glm_rotate(uniforms->model, seconds * 0.43f, axisX);
  glm_mat4_mul(viewProjection, uniforms->model, uniforms->mvp);
}

static inline void
CubeFillChecker(uint8_t *pixels) {
  uint32_t x;
  uint32_t y;

  for (y = 0u; y < CUBE_CHECKER_SIZE; y++) {
    for (x = 0u; x < CUBE_CHECKER_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color = (((x / 4u) ^ (y / 4u)) & 1u)
                ? kCubeCheckerOrange
                : kCubeCheckerBlue;
      offset = (y * CUBE_CHECKER_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = color[3];
    }
  }
}

#endif
