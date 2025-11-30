#ifdef USE_CUDA_ACCEL

#include "cuda_accel.h"

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int checkCudaInternal(cudaError_t result, const char *expr,
                             const char *file, int line) {
  if (result != cudaSuccess) {
    fprintf(stderr, "CUDA call failed: %s (%s:%d): %s\n", expr, file, line,
            cudaGetErrorString(result));
    return -1;
  }
  return 0;
}

#define CHECK_CUDA(call) checkCudaInternal((call), #call, __FILE__, __LINE__)

struct DeviceReflector {
  double x;
  double y;
  double z;
  double refl;
  double phase_offset;
  int first_image;
  int last_image;
  int is_target;
};

struct DeviceContext {
  int deviceId;
  DeviceReflector *d_reflectors;
  int reflectorCount;
  float *d_freqVec;
  int *d_Xt;
  float2 *d_Xr;
  float2 *h_partial;
};

static DeviceContext *g_devices = nullptr;
static int g_device_capacity = 0;
static int g_active_devices = 0;
static int g_total_reflectors = 0;
static int g_Nx = 0;
static int g_cuda_ready = 0;

static void release_all_devices(void) {
  if (!g_devices) {
    return;
  }

  for (int i = 0; i < g_device_capacity; ++i) {
    DeviceContext *ctx = &g_devices[i];
    if (ctx->deviceId >= 0) {
      cudaSetDevice(ctx->deviceId);
    }
    if (ctx->d_reflectors) {
      cudaFree(ctx->d_reflectors);
    }
    if (ctx->d_freqVec) {
      cudaFree(ctx->d_freqVec);
    }
    if (ctx->d_Xt) {
      cudaFree(ctx->d_Xt);
    }
    if (ctx->d_Xr) {
      cudaFree(ctx->d_Xr);
    }
    if (ctx->h_partial) {
      free(ctx->h_partial);
    }
    ctx->d_reflectors = nullptr;
    ctx->d_freqVec = nullptr;
    ctx->d_Xt = nullptr;
    ctx->d_Xr = nullptr;
    ctx->h_partial = nullptr;
    ctx->reflectorCount = 0;
  }

  free(g_devices);
  g_devices = nullptr;
  g_device_capacity = 0;
  g_active_devices = 0;
}

__global__ void accumulateKernel(const DeviceReflector *reflectors,
                                 int totalReflectors, int imageIndex,
                                 int is_rag, float px, float py, float pz,
                                 float r0, float fc, const float *freqVec,
                                 const int *Xt, int Nx, float2 *Xr_out) {
  const int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= totalReflectors) {
    return;
  }

  DeviceReflector refl = reflectors[idx];
  if (imageIndex < (refl.first_image - 1) ||
      imageIndex > (refl.last_image - 1)) {
    return;
  }

  const double dx = static_cast<double>(px) - refl.x;
  const double dy = static_cast<double>(py) - refl.y;
  const double dz = static_cast<double>(pz) - refl.z;
  const double R = sqrt(dx * dx + dy * dy + dz * dz);
  const double t_d = 2.0 * (R - r0) / c;

  const double phase_offset =
      (refl.is_target && is_rag) ? refl.phase_offset : 0.0;

  for (int n = 0; n < Nx; ++n) {
    if (!Xt[n]) {
      continue;
    }

    const double arg = -2.0 * M_PI * t_d * (fc + freqVec[n]) + phase_offset;
    const float amp = static_cast<float>(Xt[n] * refl.refl);
    const float contribReal = amp * cosf(static_cast<float>(arg));
    const float contribImag = amp * sinf(static_cast<float>(arg));
    atomicAdd(&Xr_out[n].x, contribReal);
    atomicAdd(&Xr_out[n].y, contribImag);
  }
}

int sar_cuda_init(const Reflector *backgroundReflectors,
                  int numBackgroundReflectors,
                  const Reflector *targetReflectors, int numTargetReflectors,
                  const float *freqVec, const int *Xt, int Nx) {
  if (g_cuda_ready) {
    return 0;
  }

  g_total_reflectors = numBackgroundReflectors + numTargetReflectors;
  g_Nx = Nx;

  if (g_total_reflectors == 0) {
    g_cuda_ready = 1;
    return 0;
  }

  int detectedDevices = 0;
  if (CHECK_CUDA(cudaGetDeviceCount(&detectedDevices))) {
    return -1;
  }
  if (detectedDevices <= 0) {
    fprintf(stderr, "No CUDA devices available.\n");
    return -1;
  }

  int devicesToUse = detectedDevices;
  const char *maxDevicesEnv = getenv("SAR_CUDA_MAX_DEVICES");
  if (maxDevicesEnv) {
    int requested = atoi(maxDevicesEnv);
    if (requested > 0 && requested < devicesToUse) {
      devicesToUse = requested;
    }
  }
  if (devicesToUse > g_total_reflectors) {
    devicesToUse = g_total_reflectors;
  }
  if (devicesToUse <= 0) {
    devicesToUse = 1;
  }

  g_device_capacity = devicesToUse;
  g_devices = static_cast<DeviceContext *>(
      calloc(g_device_capacity, sizeof(DeviceContext)));
  if (!g_devices) {
    return -1;
  }
  for (int i = 0; i < g_device_capacity; ++i) {
    g_devices[i].deviceId = i;
  }

  DeviceReflector *hostReflectors = static_cast<DeviceReflector *>(
      malloc(sizeof(DeviceReflector) * g_total_reflectors));
  if (!hostReflectors) {
    release_all_devices();
    return -1;
  }

  int cursor = 0;
  for (int i = 0; i < numBackgroundReflectors; ++i) {
    const Reflector *src = &backgroundReflectors[i];
    DeviceReflector *dst = &hostReflectors[cursor++];
    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
    dst->refl = src->refl;
    dst->phase_offset = src->phase_offset;
    dst->first_image = src->first_image;
    dst->last_image = src->last_image;
    dst->is_target = 0;
  }
  for (int i = 0; i < numTargetReflectors; ++i) {
    const Reflector *src = &targetReflectors[i];
    DeviceReflector *dst = &hostReflectors[cursor++];
    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
    dst->refl = src->refl;
    dst->phase_offset = src->phase_offset;
    dst->first_image = src->first_image;
    dst->last_image = src->last_image;
    dst->is_target = 1;
  }

  const int baseCount = g_total_reflectors / g_device_capacity;
  const int remainder = g_total_reflectors % g_device_capacity;
  int assigned = 0;

  for (int dev = 0; dev < g_device_capacity; ++dev) {
    DeviceContext *ctx = &g_devices[dev];
    int share = baseCount + (dev < remainder ? 1 : 0);
    ctx->reflectorCount = share;

    if (ctx->reflectorCount == 0) {
      continue;
    }

    ++g_active_devices;

    if (CHECK_CUDA(cudaSetDevice(ctx->deviceId))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    if (CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&ctx->d_reflectors),
                              sizeof(DeviceReflector) * ctx->reflectorCount))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }
    if (CHECK_CUDA(cudaMemcpy(ctx->d_reflectors, hostReflectors + assigned,
                              sizeof(DeviceReflector) * ctx->reflectorCount,
                              cudaMemcpyHostToDevice))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    if (CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&ctx->d_freqVec),
                              sizeof(float) * Nx))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }
    if (CHECK_CUDA(cudaMemcpy(ctx->d_freqVec, freqVec, sizeof(float) * Nx,
                              cudaMemcpyHostToDevice))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    if (CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&ctx->d_Xt),
                              sizeof(int) * Nx))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }
    if (CHECK_CUDA(cudaMemcpy(ctx->d_Xt, Xt, sizeof(int) * Nx,
                              cudaMemcpyHostToDevice))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    if (CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&ctx->d_Xr),
                              sizeof(float2) * Nx))) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    ctx->h_partial = static_cast<float2 *>(malloc(sizeof(float2) * Nx));
    if (!ctx->h_partial) {
      free(hostReflectors);
      release_all_devices();
      return -1;
    }

    assigned += ctx->reflectorCount;
  }

  free(hostReflectors);

  if (g_active_devices == 0) {
    release_all_devices();
    return -1;
  }

  g_cuda_ready = 1;
  return 0;
}

int sar_cuda_accumulate_pulse(int imageIndex, int is_rag, float px, float py,
                              float pz, float r0, float fc,
                              fftwf_complex *Xr_out) {
  if (!g_cuda_ready) {
    return -1;
  }

  if (g_total_reflectors == 0) {
    memset(Xr_out, 0, sizeof(fftwf_complex) * g_Nx);
    return 0;
  }

  if (!g_devices || g_active_devices == 0) {
    return -1;
  }

  memset(Xr_out, 0, sizeof(fftwf_complex) * g_Nx);

  const dim3 block(256);

  for (int i = 0; i < g_device_capacity; ++i) {
    DeviceContext *ctx = &g_devices[i];
    if (ctx->reflectorCount == 0) {
      continue;
    }

    if (CHECK_CUDA(cudaSetDevice(ctx->deviceId))) {
      return -1;
    }

    if (CHECK_CUDA(cudaMemset(ctx->d_Xr, 0, sizeof(float2) * g_Nx))) {
      return -1;
    }

    const dim3 grid((ctx->reflectorCount + block.x - 1) / block.x);
    accumulateKernel<<<grid, block>>>(
        ctx->d_reflectors, ctx->reflectorCount, imageIndex, is_rag, px, py, pz,
        r0, fc, ctx->d_freqVec, ctx->d_Xt, g_Nx, ctx->d_Xr);
    if (CHECK_CUDA(cudaGetLastError())) {
      return -1;
    }
  }

  for (int i = 0; i < g_device_capacity; ++i) {
    DeviceContext *ctx = &g_devices[i];
    if (ctx->reflectorCount == 0) {
      continue;
    }

    if (CHECK_CUDA(cudaSetDevice(ctx->deviceId))) {
      return -1;
    }

    if (CHECK_CUDA(cudaDeviceSynchronize())) {
      return -1;
    }

    if (CHECK_CUDA(cudaMemcpy(ctx->h_partial, ctx->d_Xr, sizeof(float2) * g_Nx,
                              cudaMemcpyDeviceToHost))) {
      return -1;
    }

    for (int n = 0; n < g_Nx; ++n) {
      Xr_out[n][0] += ctx->h_partial[n].x;
      Xr_out[n][1] += ctx->h_partial[n].y;
    }
  }

  return 0;
}

void sar_cuda_shutdown(void) {
  if (!g_cuda_ready && !g_devices) {
    return;
  }

  release_all_devices();
  g_total_reflectors = 0;
  g_Nx = 0;
  g_cuda_ready = 0;
}

#endif // USE_CUDA_ACCEL
