# ARTS Benchmark

Applications are from the XSOCR project.
We developed an OCR based on ARTS and connected several benchmarks to ARTS-OCR.
You can find the license information of OCR and OCR-based applications in each directory.
Here are the current applications supported in this repository:

## OCR Applications with Inter-node Hints

- CoMD_intel-chandra-tiled
- graph500
- hpcg_intel
- hpcg_intel-Eager
- hpcg_intel-Eager-Collective
- miniAMR_intel-chandra
- nekbone
- p2p
- reduction_intel
- RSBench_intel-sharedDB
- Stencil1D_intel-chandra
- Stencil1D_intel-david
- Stencil2D_intel-chandra
- Stencil2D_intel-channelEVTs
- Stencil2D_intel-jiri
- XSBench_intel-sharedDB

These applications can be run by ARTS on multiple nodes with inter-node hints enabled.

## Other OCR Applications

- basicIO
- cholesky
- CoMD_intel-chandra
- CoMD_sdsc
- CoMD_sdsc2
- curvefit
- cache-offset
- highbw
- multigen
- multigen_2
- task-priorities
- testlibs
- xeonNumaSize
- fft
- fibonacci
- globalsum
- hpgmg
- dbctrl
- prodcon
- LCS_intel-jesmin-lcs_all_db_distributed
- LCS_intel-jesmin-lcs_distributed_ST_datablocks
- LCS_intel-jesmin-lcs_shared_datablocks
- miniAMR_forkbomb
- miniAMR_intel
- miniAMR_intel-bryan
- npb-cg
- nqueens
- printf
- quicksort
- reduction_intel-chandra
- RSBench_intel
- sar_datagen-tsml
- sar_huge
- sar_large
- sar_medium
- sar_small
- sar_tiny
- sar_problem_size_scaling
- smithwaterman
- tempest_intel-bryan
- triangle
- XSBench_intel

## Known Limitations

### EDT_PROP_FINISH Termination Detection Not Supported

The OCR `EDT_PROP_FINISH` flag is designed to make an EDT's output event fire only after the EDT **and all its child EDTs** complete. This requires distributed termination detection.

**Current behavior**: In this ARTS-OCR implementation, `EDT_PROP_FINISH` behaves the same as a regular EDT - the output event fires when the parent EDT completes, regardless of whether child EDTs have finished.

**Workaround**: Applications that rely on `EDT_PROP_FINISH` semantics for proper termination should:
1. Use explicit synchronization (e.g., latch events) to track child completion
2. Call `ocrShutdown()` explicitly when the application is done

This limitation exists because ARTS's epoch-based termination detection has race conditions in single-rank mode that can cause hangs.
