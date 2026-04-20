/*
 * Copyright (C) 2020 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../../common.h"

typedef struct GPUBindGroupLayoutPriv {
  uint32_t count;
  GPUBindGroupLayoutEntry *entries;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  uint32_t binding;
  GPUBuffer *buffer;
  size_t offset;
} GPUBindGroupBindingPriv;

typedef struct GPUBindGroupPriv {
  GPUBindGroupLayout *layout;
  uint32_t count;
  GPUBindGroupBindingPriv *bindings;
} GPUBindGroupPriv;

static GPUBindGroupLayoutPriv *
gpu_layoutPriv(GPUBindGroupLayout *layout) {
  return layout ? layout->_priv : NULL;
}

static GPUBindGroupPriv *
gpu_groupPriv(GPUBindGroup *group) {
  return group ? group->_priv : NULL;
}

static const GPUBindGroupBindingPriv *
gpu_findBinding(const GPUBindGroupPriv *group, uint32_t binding) {
  uint32_t i;

  if (!group) {
    return NULL;
  }

  for (i = 0; i < group->count; i++) {
    if (group->bindings[i].binding == binding) {
      return &group->bindings[i];
    }
  }

  return NULL;
}

GPU_EXPORT
GPUDescriptorPool*
GPUCreateDescriptorPool(GPUDevice *__restrict device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->descriptor.createDescriptorPool(api, device);
}

GPU_EXPORT
int
GPUCreateBindGroupLayout(const GPUBindGroupLayoutEntry *entries,
                         uint32_t count,
                         GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayout *layout;
  GPUBindGroupLayoutPriv *priv;

  if (!entries || count == 0 || !outLayout) {
    return -1;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return -2;
  }

  priv->entries = calloc(count, sizeof(*priv->entries));
  if (!priv->entries) {
    free(priv);
    free(layout);
    return -3;
  }

  memcpy(priv->entries, entries, count * sizeof(*entries));
  priv->count = count;
  layout->_priv = priv;
  *outLayout = layout;
  return 0;
}

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutPriv *priv;

  if (!layout) {
    return;
  }

  priv = gpu_layoutPriv(layout);
  if (priv) {
    free(priv->entries);
    free(priv);
  }

  free(layout);
}

GPU_EXPORT
int
GPUCreateBindGroup(GPUBindGroupLayout *layout,
                   const GPUBindGroupEntry *entries,
                   uint32_t count,
                   GPUBindGroup **outGroup) {
  GPUBindGroup *group;
  GPUBindGroupPriv *priv;
  uint32_t i;

  if (!layout || !entries || count == 0 || !outGroup) {
    return -1;
  }

  group = calloc(1, sizeof(*group));
  priv = calloc(1, sizeof(*priv));
  if (!group || !priv) {
    free(group);
    free(priv);
    return -2;
  }

  priv->bindings = calloc(count, sizeof(*priv->bindings));
  if (!priv->bindings) {
    free(priv);
    free(group);
    return -3;
  }

  for (i = 0; i < count; i++) {
    priv->bindings[i].binding = entries[i].binding;
    priv->bindings[i].buffer = entries[i].buffer;
    priv->bindings[i].offset = entries[i].offset;
  }

  priv->layout = layout;
  priv->count = count;
  group->_priv = priv;
  *outGroup = group;
  return 0;
}

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;

  if (!group) {
    return;
  }

  priv = gpu_groupPriv(group);
  if (priv) {
    free(priv->bindings);
    free(priv);
  }

  free(group);
}

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderCommandEncoder *rce, GPUBindGroup *group) {
  const GPUBindGroupBindingPriv *binding;
  GPUBindGroupLayoutPriv *layout;
  GPUBindGroupPriv *priv;
  uint32_t i;

  if (!rce || !group) {
    return;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout) {
    return;
  }

  for (i = 0; i < layout->count; i++) {
    if (layout->entries[i].kind != GPUBindKindBuffer) {
      continue;
    }

    binding = gpu_findBinding(priv, layout->entries[i].binding);
    if (!binding || !binding->buffer) {
      continue;
    }

    switch (layout->entries[i].stage) {
      case GPUBindStageVertex:
        GPUSetVertexBuffer(rce,
                           binding->buffer,
                           binding->offset,
                           layout->entries[i].binding);
        break;
      case GPUBindStageFragment:
        GPUSetFragmentBuffer(rce,
                             binding->buffer,
                             binding->offset,
                             layout->entries[i].binding);
        break;
      default:
        break;
    }
  }
}
