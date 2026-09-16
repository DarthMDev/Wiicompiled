# Metal hardware-ray-tracing feasibility probe

This standalone test gets the native `MTLDevice` from a Dawn Metal device,
builds a single-triangle acceleration structure, traces a compute ray through
it, and validates the hit. It returns 77 when no Metal adapter or no Apple
family 9 hardware-ray-tracing device is present.

Use the same pinned Dawn package as Aurora:

```sh
cmake -S aurora-main/tests/metal_rt_probe -B build-metal-rt-probe \
  -DDawn_DIR="/absolute/path/to/dawn_prebuilt-src/lib/cmake/Dawn"
cmake --build build-metal-rt-probe
ctest --test-dir build-metal-rt-probe --output-on-failure
```

The test is a prerequisite for renderer integration. It does not yet capture
GX geometry, exchange frame textures, or enable a graphics setting.
