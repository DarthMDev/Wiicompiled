# Web bootstrap

The `Emscripten` CMake target currently builds `mkw_web_public_runtime` and
the browser-loadable `mkw_web_public_runtime_tests` module. They are the first,
game-free foundation for the browser port: they have no dependency on `Assets/`,
`generated/`, or `PulsarPacks/`, and they do not build a translated game
product.

Configure it through an installed Emscripten SDK:

```sh
emcmake cmake -S runtime -B build/web -G Ninja -DMKW_BUILD_PRODUCTS=OFF
cmake --build build/web --target mkw_web_public_runtime_check
```

The check target emits `mkw_web_public_runtime_tests.mjs`, its paired Wasm file,
and `mkw_web_public_runtime_tests.html`. Serve that build directory over a local
HTTP server and open the HTML file to run the browser test. The JavaScript
module exports `MkwWebPublicRuntimeAbiVersion`; its C++ entry point checks that
same contract. It also registers a Node-based CTest that loads the generated
Emscripten module and calls the same export; Node is included solely as the
automated test environment:

```sh
ctest --test-dir build/web --output-on-failure
```

`MKW_BUILD_PRODUCTS=ON` intentionally fails for the web target. A future local
browser preparation pipeline will supply the player's translated output only
after the web runtime has replacements for the native memory, scheduling,
renderer, storage, and audio services.

Public web targets must be created with `mkw_add_web_public_runtime`. The
helper rejects source inputs rooted in `Assets/`, `generated/`, or
`PulsarPacks/`, as well as sources outside the checkout, during CMake
configuration. This is a build-input boundary, not a substitute for the
release artifact audit.
