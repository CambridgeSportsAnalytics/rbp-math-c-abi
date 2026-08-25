# rbp-math-c-abi

**Public C ABI contract** for the [RBP Math Library](https://github.com/CambridgeSportsAnalytics) (Relevance-Based Prediction).

This repository contains the canonical header integrators use when binding C, C++, R, MATLAB, Julia, Rust `ctypes`-style FFI, and similar languages against CSA-provided **native binaries**. It is **not** the library source, runtime, or Python package.

| | |
|---|---|
| Header | [`include/rbp_math.h`](include/rbp_math.h) |
| ABI version | `RBP_ABI_VERSION` / `rbp_abi_version()` (currently **5**) |
| Shared library names | `librbp_math_lib.dylib` · `librbp_math_lib.so` · `rbp_math_lib.dll` |

The full solution (Rust engine, packaging, license tooling) lives in a **private** repository. This repository exists so the call contract can stay public without disclosing our intellectual property.

## Who this is for

- **ABI / systems engineers** writing wrappers or native call sites
- **Tooling authors** comparing header vs loaded binary (`rbp_abi_version()`)

Analysts and data scientists should use the official language packages (e.g. Python `rbp-math`) and product documentation, not this header alone.

## What you get

```text
include/rbp_math.h   # types, enums, ownership rules, function prototypes
```

The header documents:

- Matrix layout (`N×K`, row- vs column-major)
- Errors (`RbpStatus`, `rbp_last_error`)
- Ownership (`*_create` / `*_free`, results handles)
- Option codes and accepted string aliases
- When optional result fields are present

## Link sketch

```bash
# after CSA provides a release shared library for your platform
cc -I./include app.c -L/path/to/lib -lrbp_math_lib -o app
```

```c
#include "rbp_math.h"

/* verify header matches the loaded binary */
if (rbp_abi_version() != RBP_ABI_VERSION) {
    /* mismatch — rebuild against matching header / library */
}

RbpPredictionResults *res = NULL;
RbpStatus st = rbp_predict(
    y, n, x, n, k, RBP_LAYOUT_ROW_MAJOR, theta, k, NULL, &res);
if (st != RBP_OK) {
    /* message: rbp_last_error() */
    return 1;
}
/* copy out yhat/fit/… then: */
rbp_prediction_results_free(res);
```

Release binaries enforce licensing (`RBP_LICENSE` / `RBP_LICENSE_FILE`, and `RBP_SITE_ID` for site SKUs). The header only describes the ABI entry points.

## Keeping in sync

The header is maintained in the private RBP Math Library tree (`ffi/include/rbp_math.h`) and **mirrored here** on change (CI from the private repo, or a manual local sync). See [SOURCE.md](SOURCE.md) for the private commit pin. Prefer this repository (or a release tag on it) as the public pin for bindings; re-check when `RBP_ABI_VERSION` changes.

## License

See [LICENSE](LICENSE).

**Summary:** free use of this header to build software that links against **CSA-licensed RBP Math binaries**. No rights to library source, license minting/bypass, or the closed-source engine are granted by this repository.

## Support

Solution and licensing support for customers with a Library License only.
