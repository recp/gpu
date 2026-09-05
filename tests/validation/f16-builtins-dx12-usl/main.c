#include <gpu/gpu.h>

#include "../usl_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  F16_BUILTIN_CASES            = 4u,
  F16_BUILTIN_INPUT_ROWS       = F16_BUILTIN_CASES * 2u,
  F16_BUILTIN_MATH_ROWS        = 32u,
  F16_BUILTIN_GEOMETRIC_ROWS   = 8u,
  F16_BUILTIN_TRIG_ROWS        = 16u,
  F16_BUILTIN_OUTPUTS_PER_CASE = F16_BUILTIN_MATH_ROWS +
                                 F16_BUILTIN_GEOMETRIC_ROWS +
                                 F16_BUILTIN_TRIG_ROWS,
  F16_BUILTIN_OUTPUT_ROWS      = F16_BUILTIN_CASES *
                                 F16_BUILTIN_OUTPUTS_PER_CASE
};

static const float kInputs[F16_BUILTIN_INPUT_ROWS][4] = {
  {0.0f, -0.0f, 0.0f, -0.0f},
  {0.0f, 0.0f, -0.0f, -0.0f},
  {0.5f, -0.5f, 1.0f, 1.5f},
  {0.75f, -0.75f, -1.0f, 2.0f},
  {5.9604644775390625e-8f, -5.9604644775390625e-8f,
   6.103515625e-5f, 65504.0f},
  {1.0f, -1.0f, 2.0f, 2.0f},
  {INFINITY, -INFINITY, NAN, -1.0f},
  {1.0f, 1.0f, 1.0f, NAN}
};

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
    return NULL;
  }
  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static uint32_t
float_bits(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static uint16_t
float_to_half_bits(float value) {
  uint32_t bits = float_bits(value);
  uint32_t sign = (bits >> 16u) & 0x8000u;
  uint32_t exponent = (bits >> 23u) & 0xffu;
  uint32_t mantissa = bits & 0x7fffffu;
  int32_t  halfExponent;

  if (exponent == 0xffu) {
    uint32_t payload;

    if (mantissa == 0u) return (uint16_t)(sign | 0x7c00u);
    payload = mantissa >> 13u;
    if (payload == 0u) payload = 1u;
    return (uint16_t)(sign | 0x7c00u | payload);
  }
  halfExponent = (int32_t)exponent - 127 + 15;
  if (halfExponent <= 0) {
    uint32_t halfway, remainder, rounded, shift;

    if (halfExponent < -10) return (uint16_t)sign;
    mantissa |= 0x800000u;
    shift     = (uint32_t)(14 - halfExponent);
    rounded   = mantissa >> shift;
    remainder = mantissa & ((1u << shift) - 1u);
    halfway   = 1u << (shift - 1u);
    if (remainder > halfway ||
        (remainder == halfway && (rounded & 1u) != 0u)) {
      rounded++;
    }
    return (uint16_t)(sign | rounded);
  }
  if (halfExponent >= 0x1f) return (uint16_t)(sign | 0x7c00u);
  {
    uint32_t remainder = mantissa & 0x1fffu;
    uint32_t rounded   = mantissa >> 13u;

    if (remainder > 0x1000u ||
        (remainder == 0x1000u && (rounded & 1u) != 0u)) {
      rounded++;
      if (rounded == 0x400u) {
        rounded = 0u;
        halfExponent++;
        if (halfExponent >= 0x1f)
          return (uint16_t)(sign | 0x7c00u);
      }
    }
    return (uint16_t)(sign | ((uint32_t)halfExponent << 10u) | rounded);
  }
}

static float
half_bits_to_float(uint16_t bits) {
  uint32_t sign     = ((uint32_t)bits & 0x8000u) << 16u;
  uint32_t exponent = ((uint32_t)bits >> 10u) & 0x1fu;
  uint32_t mantissa = (uint32_t)bits & 0x03ffu;
  uint32_t resultBits;
  float    result;

  if (exponent == 0u) {
    if (mantissa == 0u) {
      resultBits = sign;
    } else {
      int32_t unbiased = -14;

      while ((mantissa & 0x0400u) == 0u) {
        mantissa <<= 1u;
        unbiased--;
      }
      mantissa  &= 0x03ffu;
      resultBits = sign | ((uint32_t)(unbiased + 127) << 23u) |
                   (mantissa << 13u);
    }
  } else if (exponent == 0x1fu) {
    resultBits = sign | 0x7f800000u | (mantissa << 13u);
  } else {
    resultBits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
  }
  memcpy(&result, &resultBits, sizeof(result));
  return result;
}

static float
half_round(float value) {
  return half_bits_to_float(float_to_half_bits(value));
}

static float
half_add(float left, float right) {
  return half_round(left + right);
}

static float
half_sub(float left, float right) {
  return half_round(left - right);
}

static float
half_mul(float left, float right) {
  return half_round(left * right);
}

static float
half_div(float left, float right) {
  return half_round(left / right);
}

static float
half_fma(float left, float right, float addend) {
  return half_round(fmaf(left, right, addend));
}

static uint16_t
ordered_half_bits(uint16_t bits) {
  return (bits & 0x8000u) != 0u ? (uint16_t)~bits
                                : (uint16_t)(bits | 0x8000u);
}

static uint16_t
half_ulp_distance(float left, float right) {
  uint16_t leftBits  = ordered_half_bits(float_to_half_bits(left));
  uint16_t rightBits = ordered_half_bits(float_to_half_bits(right));

  return leftBits > rightBits ? (uint16_t)(leftBits - rightBits)
                              : (uint16_t)(rightBits - leftBits);
}

static int
value_matches(const char *name,
              uint32_t    testCase,
              uint32_t    lane,
              float       actual,
              float       expected,
              uint16_t    ulpLimit) {
  uint16_t actualBits, expectedBits, ulp;

  if (isnan(expected)) {
    if (isnan(actual)) return 1;
  } else if (actual == expected) {
    if (expected != 0.0f || float_bits(actual) == float_bits(expected))
      return 1;
  } else if (!isnan(actual) && !isinf(actual) && !isinf(expected) &&
             half_ulp_distance(actual, expected) <= ulpLimit) {
    return 1;
  }
  actualBits   = float_to_half_bits(actual);
  expectedBits = float_to_half_bits(expected);
  ulp          = isnan(actual) || isnan(expected)
                   ? UINT16_MAX
                   : half_ulp_distance(actual, expected);
  fprintf(stderr,
          "Direct DXIL F16 %s mismatch at case %u lane %u: expected %.9g "
          "(0x%04x), got %.9g (0x%04x), %u ULP (limit %u)\n",
          name,
          testCase,
          lane,
          expected,
          (unsigned)expectedBits,
          actual,
          (unsigned)actualBits,
          (unsigned)ulp,
          (unsigned)ulpLimit);
  return 0;
}

static int
validate_results(const float output[F16_BUILTIN_OUTPUT_ROWS][4]);

static float
half_abs(float value) {
  return half_round(fabsf(value));
}

static float
half_min(float left, float right) {
  return half_round(fminf(left, right));
}

static float
half_max(float left, float right) {
  return half_round(fmaxf(left, right));
}

static float
half_clamp(float value, float low, float high) {
  return half_round(fminf(fmaxf(value, low), high));
}

static float
half_sign(float value) {
  return value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;
}

static float
half_step(float edge, float value) {
  return value < edge ? 0.0f : 1.0f;
}

static float
half_floor_mod(float left, float right) {
  float quotient = half_div(left, right);
  float integral = half_round(floorf(quotient));

  return half_fma(half_round(-integral), right, left);
}

static float
half_mix(float left, float right, float weight) {
  return half_fma(weight, half_sub(right, left), left);
}

static float
half_smoothstep(float low, float high, float value) {
  float ratio = half_div(half_sub(value, low), half_sub(high, low));
  float t     = half_clamp(ratio, 0.0f, 1.0f);

  if (isnan(ratio)) return NAN;
  return half_mul(half_mul(t, t), half_fma(t, -2.0f, 3.0f));
}

static float
half_pow(float base, float exponent) {
  return half_round(exp2f(half_mul(half_round(log2f(base)), exponent)));
}

static float
half_math_expected(uint32_t row, float a, float b) {
  float positiveA = half_add(half_abs(a), 0.25f);
  float positiveB = half_add(half_abs(b), 0.25f);

  switch (row) {
    case 0u: return half_round(floorf(a));
    case 1u: return half_round(ceilf(a));
    case 2u: return half_round(nearbyintf(a));
    case 3u: return half_round(truncf(a));
    case 4u: return half_sub(a, half_round(floorf(a)));
    case 5u: return half_round(sqrtf(positiveA));
    case 6u: return half_round(1.0f / sqrtf(positiveA));
    case 7u: return half_round(1.0f / sqrtf(positiveB));
    case 8u: return half_div(1.0f, a);
    case 9u:
      return half_round(exp2f(half_mul(a, half_round(1.4426950408889634f))));
    case 10u: return half_round(exp2f(a));
    case 11u:
      return half_mul(half_round(log2f(positiveA)),
                      half_round(0.6931471805599453f));
    case 12u: return half_round(log2f(positiveA));
    case 13u: return half_mul(a, half_round(57.29577951308232f));
    case 14u: return half_mul(a, half_round(0.017453292519943295f));
    case 15u: return half_clamp(a, 0.0f, 1.0f);
    case 16u: return half_abs(a);
    case 17u: return half_sign(a);
    case 18u: return half_pow(positiveA, b);
    case 19u: return half_floor_mod(a, b);
    case 20u: return half_round(fmodf(a, b));
    case 21u: return half_min(a, b);
    case 22u: return half_max(a, b);
    case 23u: return half_clamp(a, -1.0f, 1.0f);
    case 24u: return half_step(b, a);
    case 25u: return half_round(copysignf(a, b));
    case 26u: return half_mix(a, b, 0.25f);
    case 27u: return half_mix(a, b, 0.75f);
    case 28u: return half_smoothstep(-1.0f, 1.0f, a);
    case 29u: return half_fma(a, b, 0.5f);
    case 30u:
      return half_div(half_sub(half_round(-a), a), half_sub(b, a));
    case 31u:
      return half_mix(
        half_round(-b),
        b,
        half_div(half_sub(half_round(-a), a), half_sub(b, a))
      );
    default: return NAN;
  }
}

static float
half_dot4(const float left[4], const float right[4]) {
  float sum = half_mul(left[0], right[0]);

  for (uint32_t lane = 1u; lane < 4u; lane++)
    sum = half_fma(left[lane], right[lane], sum);
  return sum;
}

static void
half_geometric_expected(const float a[4],
                        const float b[4],
                        float       expected[F16_BUILTIN_GEOMETRIC_ROWS][4]) {
  float difference[4], incident[4], projected[4];
  float dotAB = half_dot4(a, b);
  float dotAA = half_dot4(a, a);
  float dotBB = half_dot4(b, b);
  float inverseLength = half_round(1.0f / sqrtf(dotAA));
  float projectScale  = half_div(dotAB, dotBB);
  float reflectScale  = half_mul(-2.0f, dotAB);
  float eta            = 0.75f;
  float etaSquared     = half_mul(eta, eta);
  float d2MinusOne     = half_fma(dotAB, dotAB, -1.0f);
  float k              = half_fma(etaSquared, d2MinusOne, 1.0f);
  float safeK          = k < 0.0f ? 0.0f : k;
  float refractScale   = half_fma(eta, dotAB,
                                  half_round(sqrtf(safeK)));

  for (uint32_t lane = 0u; lane < 4u; lane++) {
    difference[lane] = half_sub(a[lane], b[lane]);
    incident[lane]   = half_mul(eta, a[lane]);
    projected[lane]  = half_mul(projectScale, b[lane]);
  }
  expected[0][0] = dotAB;
  expected[0][1] = half_round(sqrtf(dotAA));
  expected[0][2] = half_round(sqrtf(half_dot4(difference, difference)));
  expected[0][3] = 0.0f;
  expected[1][0] = half_sub(half_mul(a[1], b[2]), half_mul(a[2], b[1]));
  expected[1][1] = half_sub(half_mul(a[2], b[0]), half_mul(a[0], b[2]));
  expected[1][2] = half_sub(half_mul(a[0], b[1]), half_mul(a[1], b[0]));
  expected[1][3] = 1.0f;
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    expected[2][lane] = half_mul(a[lane], inverseLength);
    expected[3][lane] = half_fma(reflectScale, b[lane], a[lane]);
    expected[4][lane] = projected[lane];
    expected[5][lane] = half_sub(a[lane], projected[lane]);
    expected[6][lane] = k < 0.0f
                          ? 0.0f
                          : half_fma(half_round(-refractScale),
                                     b[lane],
                                     incident[lane]);
    expected[7][lane] = dotAB < 0.0f ? a[lane] : half_round(-a[lane]);
  }
}

static float
half_trig_expected(uint32_t row, float a, float b) {
  switch (row) {
    case 0u: return half_round(sinf(a));
    case 1u: return half_round(cosf(a));
    case 2u: return half_round(tanf(a));
    case 3u: return half_round(asinf(a));
    case 4u: return half_round(acosf(a));
    case 5u: return half_round(atanf(a));
    case 6u: return half_round(atanf(half_div(1.0f, a)));
    case 7u: return half_round(acosf(half_div(1.0f, a)));
    case 8u: return half_round(asinf(half_div(1.0f, a)));
    case 9u: return half_round(atan2f(a, b));
    case 10u: return half_round(sinhf(a));
    case 11u: return half_round(coshf(a));
    case 12u: return half_round(tanhf(a));
    case 13u: return half_round(asinhf(a));
    case 14u: return half_round(acoshf(a));
    case 15u: return half_round(atanhf(a));
    default: return NAN;
  }
}

static int
validate_results(const float output[F16_BUILTIN_OUTPUT_ROWS][4]) {
  static const char *mathNames[F16_BUILTIN_MATH_ROWS] = {
    "floor", "ceil", "round", "trunc", "fract", "sqrt", "rsqrt",
    "inversesqrt", "rcp", "exp", "exp2", "log", "log2", "degrees",
    "radians", "saturate", "abs", "sign", "pow", "mod", "fmod",
    "min", "max", "clamp", "step", "copysign", "mix", "lerp",
    "smoothstep", "fma", "inverselerp", "remap"
  };
  static const char *trigNames[F16_BUILTIN_TRIG_ROWS] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "acot", "asec",
    "acsc", "atan2", "sinh", "cosh", "tanh", "asinh", "acosh",
    "atanh"
  };
  static const char *geometricNames[F16_BUILTIN_GEOMETRIC_ROWS] = {
    "dot-length-distance", "cross", "normalize", "reflect", "project",
    "reject", "refract", "faceforward"
  };
  int ok = 1;

  for (uint32_t testCase = 0u; testCase < F16_BUILTIN_CASES; testCase++) {
    float a[4], b[4];
    float geometric[F16_BUILTIN_GEOMETRIC_ROWS][4];
    uint32_t base = testCase * F16_BUILTIN_OUTPUTS_PER_CASE;

    for (uint32_t lane = 0u; lane < 4u; lane++) {
      a[lane] = half_round(kInputs[testCase * 2u][lane]);
      b[lane] = half_round(kInputs[testCase * 2u + 1u][lane]);
      for (uint32_t row = 0u; row < F16_BUILTIN_MATH_ROWS; row++) {
        float expected = half_math_expected(row, a[lane], b[lane]);
        uint16_t limit = row >= 18u ? 16u : 8u;

        if (row == 17u && output[base + row][lane] == 0.0f &&
            expected == 0.0f) {
          continue;
        }
        if (!value_matches(mathNames[row],
                           testCase,
                           lane,
                           output[base + row][lane],
                           expected,
                           limit)) {
          ok = 0;
        }
      }
    }
    half_geometric_expected(a, b, geometric);
    for (uint32_t row = 0u; row < F16_BUILTIN_GEOMETRIC_ROWS; row++) {
      for (uint32_t lane = 0u; lane < 4u; lane++) {
        if (!value_matches(geometricNames[row],
                           testCase,
                           lane,
                           output[base + F16_BUILTIN_MATH_ROWS + row][lane],
                           geometric[row][lane],
                           8u)) {
          ok = 0;
        }
      }
    }
    for (uint32_t row = 0u; row < F16_BUILTIN_TRIG_ROWS; row++) {
      for (uint32_t lane = 0u; lane < 4u; lane++) {
        uint16_t limit = testCase == 2u && lane == 3u &&
                         (row == 1u || row == 2u)
                           ? 64u
                           : 8u;

        if (!value_matches(
              trigNames[row],
              testCase,
              lane,
              output[base + F16_BUILTIN_MATH_ROWS +
                     F16_BUILTIN_GEOMETRIC_ROWS + row][lane],
              half_trig_expected(row, a[lane], b[lane]),
              limit)) {
          ok = 0;
        }
      }
    }
  }
  return ok;
}

int
main(int argc, char **argv) {
  GPUFeature                    feature = GPU_FEATURE_SHADER_F16;
  GPUInstance                  *instance = NULL;
  GPUAdapter                   *adapter = NULL;
  GPUDevice                    *device = NULL;
  GPUQueue                     *queue = NULL;
  GPUShaderLibrary             *library = NULL;
  GPUShaderLayout              *shaderLayout = NULL;
  GPUComputePipeline           *pipeline = NULL;
  GPUBuffer                    *buffers[2] = {0};
  GPUBindGroup                 *bindGroup = NULL;
  GPUCommandBuffer             *cmdb = NULL;
  GPUComputePassEncoder        *pass = NULL;
  GPUFence                     *fence = NULL;
  void                         *artifact = NULL;
  GPUInstanceCreateInfo         instanceInfo = {0};
  GPUDeviceCreateInfo           deviceInfo = {0};
  GPURuntimeConfig              runtimeConfig = {0};
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBindGroupEntry             groupEntries[2] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  GPUQueueSubmitInfo            submitInfo = {0};
  float output[F16_BUILTIN_OUTPUT_ROWS][4] = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  const char                    *artifactPath;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint64_t                       bufferSizes[2];
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr,
            "usage: gpu-f16-builtins-dx12-usl [f16_builtins.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f16_builtins.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "Direct3D 12 F16 builtin artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  result = GPUCreateInstance(&instanceInfo, &instance);
  if (result != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 F16 instance creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter || !GPUIsFeatureSupported(adapter, feature)) {
    fprintf(stderr, "Direct3D 12 F16 adapter is unavailable\n");
    goto cleanup;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  result = GPUCreateDevice(adapter, &deviceInfo, &device);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (result != GPU_OK || !device || !queue ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "Direct3D 12 F16 device creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK) {
    fprintf(stderr, "Direct3D 12 F16 runtime configuration failed\n");
    goto cleanup;
  }
  result = gpu_test_create_shader_library_from_usl(device,
                                                    artifact,
                                                    artifactSize,
                                                    &library);
  if (result != GPU_OK || !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Direct3D 12 F16 shader setup failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 2u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
      layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_STORAGE_BUFFER) {
    fprintf(stderr, "Unexpected Direct3D 12 F16 reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-f16-builtins";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f16_builtins";
  result = GPUCreateComputePipeline(device, &pipelineInfo, &pipeline);
  if (result != GPU_OK || !pipeline) {
    fprintf(stderr, "Direct3D 12 F16 pipeline creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  bufferSizes[0] = sizeof(kInputs);
  bufferSizes[1] = sizeof(output);
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t binding = 0u; binding < 2u; binding++) {
    bufferInfo.sizeBytes = bufferSizes[binding];
    result = GPUCreateBuffer(device, &bufferInfo, &buffers[binding]);
    if (result != GPU_OK || !buffers[binding] ||
        GPUQueueWriteBuffer(queue,
                            buffers[binding],
                            0u,
                            binding == 0u ? (const void *)kInputs
                                          : (const void *)output,
                            bufferSizes[binding]) != GPU_OK) {
      fprintf(stderr, "Direct3D 12 F16 buffer %u failed (%d)\n",
              binding,
              (int)result);
      goto cleanup;
    }
    groupEntries[binding].binding       = binding;
    groupEntries[binding].bindingType   = binding == 0u
                                            ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                            : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = bufferSizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-f16-builtins-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  result = GPUCreateBindGroup(device, &groupInfo, &bindGroup);
  if (result != GPU_OK || !bindGroup ||
      GPUAcquireCommandBuffer(queue,
                              "dx12-native-f16-builtins",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 F16 bind/command failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f16-builtins");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 F16 compute pass failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, F16_BUILTIN_CASES, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  result = GPUCreateFence(device, NULL, &fence);
  if (result != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 F16 fence creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[1],
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !validate_results(output)) {
    fprintf(stderr, "Direct3D 12 F16 readback validation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(buffers[0]);
  GPUDestroyBuffer(buffers[1]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  if (!ok) return 1;
  puts("Direct3D 12 native F16 builtin validation passed");
  return 0;
}
