#include "test.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/sampler_internal.h"

static GPUSampler gScopedSampler;
static uint32_t   gScopedSamplerCreateCalls;
static uint32_t   gScopedSamplerDestroyCalls;

static GPUResult
create_scoped_sampler(GPUApi                    * __restrict api,
                      GPUDevice                 * __restrict device,
                      const GPUSamplerCreateInfo *info,
                      bool                       staticIfSupported,
                      GPUSampler               **outSampler) {
  (void)api;
  (void)device;
  (void)info;
  (void)staticIfSupported;
  memset(&gScopedSampler, 0, sizeof(gScopedSampler));
  *outSampler = &gScopedSampler;
  gScopedSamplerCreateCalls++;
  return GPU_OK;
}

static void
destroy_scoped_sampler(GPUSampler * __restrict sampler) {
  (void)sampler;
  gScopedSamplerDestroyCalls++;
}

static GPUSamplerDesc
valid_sampler_desc(void) {
  GPUSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter = GPU_FILTER_LINEAR;
  desc.magFilter = GPU_FILTER_LINEAR;
  desc.mipFilter = GPU_MIP_FILTER_LINEAR;
  desc.addressU = GPU_ADDRESS_MODE_REPEAT;
  desc.addressV = GPU_ADDRESS_MODE_REPEAT;
  desc.addressW = GPU_ADDRESS_MODE_REPEAT;
  return desc;
}

static int
check_sampler_device_dispatch(GPUDevice *activeDevice) {
  GPUSampler              *sampler;
  GPUSamplerCreateInfo    info = {0};
  GPUDevice               device = {0};
  GPUApi                  scopedApi;

  if (!activeDevice || !gpuDeviceApi(activeDevice)) {
    fprintf(stderr, "sampler dispatch has no device api\n");
    return 0;
  }

  scopedApi = *gpuDeviceApi(activeDevice);
  scopedApi.sampler.createSampler = create_scoped_sampler;
  scopedApi.sampler.destroySampler = destroy_scoped_sampler;
  device._api                      = &scopedApi;
  gScopedSamplerCreateCalls        = 0u;
  gScopedSamplerDestroyCalls       = 0u;

  info.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.desc             = valid_sampler_desc();
  sampler = NULL;
  if (GPUCreateSampler(&device, &info, false, &sampler) != GPU_OK ||
      sampler != &gScopedSampler || sampler->device != &device) {
    fprintf(stderr, "sampler device dispatch failed\n");
    return 0;
  }
  GPUDestroySampler(sampler);

  if (gScopedSamplerCreateCalls != 1u ||
      gScopedSamplerDestroyCalls != 1u) {
    fprintf(stderr, "sampler dispatch called wrong backend\n");
    return 0;
  }

  return 1;
}

static int
check_sampler_validation(GPUDevice *device) {
  GPUSamplerCreateInfo info;
  GPUSampler *sampler;

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "reflection-sampler";
  info.desc = valid_sampler_desc();

  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(NULL, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted null device\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  if (GPUCreateSampler(device, &info, false, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "sampler create accepted null output\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted wrong sType\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted short structSize\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.desc.minFilter = (GPUFilter)99;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted invalid min filter\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.desc = valid_sampler_desc();
  info.desc.addressV = (GPUAddressMode)99;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted invalid address mode\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.desc = valid_sampler_desc();
  sampler = NULL;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_OK || !sampler) {
    fprintf(stderr, "sampler create rejected valid dynamic sampler\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  GPUDestroySampler(sampler);

  sampler = NULL;
  if (GPUCreateSampler(device, &info, true, &sampler) != GPU_OK || !sampler) {
    fprintf(stderr, "sampler create rejected valid static-if-supported sampler\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  GPUDestroySampler(sampler);

  return 1;
}

int
gpu_test_sampler(GPUDevice *device) {
  return check_sampler_device_dispatch(device) &&
         check_sampler_validation(device);
}
