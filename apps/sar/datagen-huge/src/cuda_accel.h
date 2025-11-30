#pragma once

#include "common.h"
#include <fftw3.h>

#ifdef __cplusplus
extern "C" {
#endif

int sar_cuda_init(const Reflector *backgroundReflectors,
                  int numBackgroundReflectors,
                  const Reflector *targetReflectors, int numTargetReflectors,
                  const float *freqVec, const int *Xt, int Nx);

int sar_cuda_accumulate_pulse(int imageIndex, int is_rag, float px, float py,
                              float pz, float r0, float fc,
                              fftwf_complex *Xr_out);

void sar_cuda_shutdown(void);

#ifdef __cplusplus
}
#endif
