/**
 * @file rbp_math.h
 * @brief RBP Math Library — C ABI (Foreign Function Interface)
 *
 * Stable entry points for Python / R / MATLAB (and other) wrappers.
 * Link against the release shared library (`librbp_math_lib.dylib` /
 * `librbp_math_lib.so` / `rbp_math_lib.dll`). This header is the **compile-time
 * contract**; the shared library is the **runtime implementation**. They must
 * agree on ::RBP_ABI_VERSION.
 *
 * # Who this is for
 * - **ABI / systems engineers**: functions, ownership, layouts, errors, versioning
 *   (this file).
 * - **Analysts / data scientists**: concepts (fit, censoring, grid retain, ysolo)
 *   live in higher-level docs and language wrappers (e.g. Python `rbp_math`), not
 *   here. This header documents sizes and presence rules, not RBP methodology.
 *
 * # Link example (macOS / Linux-style)
 * @code
 *   cc -Ipath/to/include app.c -Lpath/to/lib -lrbp_math_lib -o app
 *   # and ensure the .dylib / .so is on rpath or DYLD_LIBRARY_PATH / LD_LIBRARY_PATH
 * @endcode
 *
 * # Minimal call sketch (predict)
 * @code
 *   RbpPredictionResults *res = NULL;
 *   double y[N], x[N * K], theta[K], yhat[T_max];
 *   if (rbp_abi_version() != RBP_ABI_VERSION) { return -1; } // ABI mismatch
 *   RbpStatus st = rbp_predict(
 *       y, N, x, N, K, RBP_LAYOUT_ROW_MAJOR, theta, K, NULL, &res);
 *   if (st != RBP_OK) {
 *       fprintf(stderr, "%s\n", rbp_last_error());
 *       return 1;
 *   }
 *   size_t T = rbp_results_num_thresholds(res);
 *   st = rbp_results_copy_yhat(res, yhat, T);
 *   rbp_prediction_results_free(res);
 * @endcode
 *
 * # Matrix convention
 * Logical shape is always **N × K** (observations × variables).
 * Pass physical storage order via ::RbpLayout:
 *   - ::RBP_LAYOUT_ROW_MAJOR — C / NumPy default: `(i,j)` at `i*ncols + j`
 *   - ::RBP_LAYOUT_COL_MAJOR — R / MATLAB / Fortran: `(i,j)` at `i + j*nrows`
 * Buffers are contiguous `double` (or `uint8_t` for include). No extra alignment
 * beyond ordinary C array alignment is required.
 *
 * # Buffers and sizes
 * - Input `y` length N; `theta` length K; `x` has N×K elements in the chosen layout.
 * - `n` must equal `n_rows`; `k` must equal `n_cols` on model / insight calls.
 * - Result `copy_*` / insight `out` buffers are **caller-allocated**.
 * - Flat vectors: `len` must be ≥ the scalar length (usually T, N, K, or 1).
 * - 2-D copies: call the matching `*_dims` (rows, cols), allocate `rows*cols`,
 *   then `copy_*` with `len ≥ rows*cols` and the desired ::RbpLayout.
 * - 3-D grid cells (`weights_cells`, `xi_solo_cells`): shape reported as
 *   `(d0,d1,d2)` ≈ `(N,T,Q)`; C-order index `i0*(d1*d2)+i1*d2+i2`;
 *   `len ≥ d0*d1*d2`.
 *
 * # Errors
 * Most functions return ::RbpStatus. On any non-::RBP_OK value, call
 * ::rbp_last_error() for a human-readable message.
 * - Error string is **thread-local**; do **not** free it.
 * - Lifetime ends at the next FFI call on **the same thread** that sets an error
 *   (or thread exit). Copy the string if you need to keep it.
 *
 * # Threading
 * - Distinct model calls on different threads are supported (library uses internal
 *   parallelism where enabled).
 * - A given options / results **handle must not** be used concurrently (no free
 *   while another thread is reading or writing through it).
 * - ::rbp_last_error is per-thread; process-wide settings such as
 *   ::rbp_set_percentile_value_algorithm affect the whole process.
 *
 * # Ownership
 * - `*_create` → caller owns; free with matching `*_free` (null-safe).
 * - `rbp_predict` / `rbp_maxfit` / `rbp_grid` write an owned
 *   ::RbpPredictionResults* into `out_results` on success; free with
 *   ::rbp_prediction_results_free. On failure `*out_results` is set to null.
 * - `options == NULL` on model calls means “use library defaults”.
 * - Optional `cov_inv` on insights: NULL ⇒ compute inverse covariance from X
 *   (and `cov_inv_layout` is ignored).
 *
 * # Results presence (when to call has_* / copy_*)
 * Notation: N = observations, K = variables, T = thresholds on this handle,
 * Q = attribute combinations (grid retain). MaxFit collapses the result to the
 * winning threshold (typically T = 1). Grid composite handles typically T = 1.
 *
 * Always present after success of predict / maxfit / grid (length T unless noted):
 *   thresholds, yhat, fit, adjusted_fit, agreement, asymmetry, k_fit,
 *   outlier_influence.
 *
 * Often present (predict / maxfit; may be absent on lean grid):
 *   weights, weights_excluded, include        — 2-D, typically N×T
 *   auxiliary (phi, lambda_sq, …)             — length T each
 *   relevance, similarity, info_x             — length N
 *   info_theta                                — length 1
 *   weights_concentration                     — length T
 *   y_solo                                    — length N
 *   xi_solo                                   — 2-D
 *   ysolo_* summary stats                     — length typically T
 *   ysolo_distribution histogram              — when allocated on the handle
 *
 * MaxFit only (when filled):
 *   maxfit_index — length = original search T (not the collapsed T=1)
 *
 * Optional linear:
 *   yhat_linear (T) — only if include_linear_regression was enabled
 *
 * Grid (even when retain is off):
 *   yhat length 1 holds the composite prediction; grid insights
 *   (variable_weights / mctc / mctp / cctp; K as documented below).
 *   xi_solo_composite (N) and composite ysolo_sigma are filled only when
 *   retain includes ysolo_distribution.
 *
 * Grid only when retain is enabled (see retain options):
 *   grid cells — k_cells, combi_cells, ysolo_cells, per-censor yhat_cells, …
 * Query with `rbp_results_has_*` before copying; missing optional fields return
 * ::RBP_ERR_INVALID_ARG on `copy_*`.
 *
 * # Option integer codes (int32 setters)
 *   censor_type:       0=relevance, 1=similarity,
 *                      2=both (maxfit/grid only; invalid for predict)
 *   censor_unit:       0=score, 1=percent
 *   censor_operator:   0=gt, 1=lt, 2=gte, 3=lte
 *                      (new option handles default to 2=gte)
 *   prediction_scale:  0=response (y scale), 1=logistic
 *   adj_fit_multiplier:0=identity, 1=K, 2=log
 *   inv_method:        0=gaussian/LU, 1=cholesky, 2=pseudoinverse
 *   missing_moments:   0=pairwise (default), 1=complete
 *                      (predict/maxfit only; Grid chooses per combination)
 *   inner_parallel:    0=auto (default), 1=off  (maxfit/grid only)
 *   verify_missing_data (deprecated alias for missing_moments):
 *                      0 = pairwise, non-zero = complete
 *                      (predict/maxfit only; not a Grid option)
 *   include_linear_regression / verbose:
 *                      0 = off, non-zero = on
 *   percentile algorithm (process-wide): 0=full_sort, 1=order_statistics
 *
 * # Option string forms (UTF-8, case-insensitive unless noted)
 * Strings apply only where a `*_str` setter exists (censor_*, objective,
 * inner_parallel, retain, percentile algorithm). Int-only fields have no
 * `_str` setter at this ABI.
 *
 *   censor_type:     "relevance" | "similarity" | "both"
 *   censor_unit:     "score" | "percent"
 *   censor_operator: "greater_than"|"gt"|">" | "less_than"|"lt"|"<" |
 *                    "greater_than_or_equal_to"|"gte"|">=" |
 *                    "less_than_or_equal_to"|"lte"|"<="
 *   maxfit objective:"fit" | "adjusted_fit" | "kfit"
 *   inner_parallel:  "auto" | "off" | "false" | "0" | "sequential" | "seq"
 *   percentile alg:  "full_sort"|"fullsort"|"sort" |
 *                    "order_statistics"|"order_stats"|"select_nth"|"nth"
 *   retain policy:   "true"|"1"|"all" | "false"|"0"|"none" |
 *                    comma-separated keys (optional `[…]` brackets):
 *                    yhat_cells, adjusted_fit_cells, n_cells, weights_cells,
 *                    k_cells, combi_cells, ysolo_distribution
 *
 * # Licensing
 * Release builds enforce entitlements at predict / maxfit / grid entry points.
 * Set `RBP_LICENSE` (token) or `RBP_LICENSE_FILE` (path); for site SKUs also set
 * `RBP_SITE_ID`. Licensing UX is documented in the product docs (client license
 * guide), not in this ABI header.
 *
 * # ABI versioning
 * ::RBP_ABI_VERSION (this header) must match ::rbp_abi_version() from the loaded
 * library. Bump the constant when breaking this contract (signatures, enums,
 * ownership). Additive symbols may also bump the version. Prefer checking at
 * process start.
 */

#ifndef RBP_MATH_H
#define RBP_MATH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Compile-time ABI version; must match ::rbp_abi_version(). */
#define RBP_ABI_VERSION 4

/**
 * Status codes returned by FFI entry points.
 * On any non-::RBP_OK value, inspect ::rbp_last_error().
 */
typedef enum RbpStatus {
    RBP_OK = 0,                 /**< Success */
    RBP_ERR_NULL = 1,           /**< Required pointer was null */
    RBP_ERR_DIM = 2,            /**< Shape / length mismatch or buffer too small */
    RBP_ERR_INVALID_ARG = 3,    /**< Bad enum, string, or argument */
    RBP_ERR_LICENSE = 4,        /**< Missing / invalid / exceeded license */
    RBP_ERR_COMPUTE = 5,        /**< Numerical / algorithm failure (or caught panic) */
    RBP_ERR_OUT_OF_MEMORY = 6   /**< Allocation failure (reserved) */
} RbpStatus;

/**
 * Physical layout of a caller-owned matrix buffer.
 * Logical indices are still (row = observation, col = variable).
 */
typedef enum RbpLayout {
    RBP_LAYOUT_ROW_MAJOR = 0,   /**< C / NumPy */
    RBP_LAYOUT_COL_MAJOR = 1    /**< R / MATLAB / Fortran */
} RbpLayout;

/** Opaque predict configuration. */
typedef struct RbpPredictOptions RbpPredictOptions;
/** Opaque MaxFit configuration (embeds predict base options). */
typedef struct RbpMaxFitOptions RbpMaxFitOptions;
/** Opaque Grid configuration (embeds predict base options). */
typedef struct RbpGridOptions RbpGridOptions;
/** Opaque prediction / maxfit / grid results handle. */
typedef struct RbpPredictionResults RbpPredictionResults;

/* =========================================================================
 * Core
 * ========================================================================= */

/**
 * @return Runtime ABI version compiled into the shared library.
 * Compare to ::RBP_ABI_VERSION from this header.
 */
uint32_t rbp_abi_version(void);

/**
 * Most recent error message on this thread, or empty string if none.
 * @return NUL-terminated C string. Do **not** free. Lifetime ends at the next
 *         FFI call on this thread that sets an error (or thread exit).
 */
const char *rbp_last_error(void);

/**
 * Process-wide percentile cut algorithm used by predict / maxfit / grid
 * when `censor_unit` is percent.
 * @param algorithm 0 = full_sort (legacy default), 1 = order_statistics (select_nth).
 */
RbpStatus rbp_set_percentile_value_algorithm(int32_t algorithm);

/** @return 0 = full_sort, 1 = order_statistics. */
int32_t rbp_get_percentile_value_algorithm(void);

/**
 * Same as ::rbp_set_percentile_value_algorithm with a string:
 * `"full_sort"` / `"fullsort"` / `"sort"`, or
 * `"order_statistics"` / `"order_stats"` / `"select_nth"` / `"nth"`
 * (case-insensitive).
 */
RbpStatus rbp_set_percentile_value_algorithm_str(const char *value);

/* =========================================================================
 * Predict options
 * ========================================================================= */

/**
 * Allocate options with library defaults (threshold `[0.5]`, censor type
 * relevance, unit percent, operator greater-than, and other PredictOptions
 * defaults).
 * @return Owned handle; free with ::rbp_predict_options_free.
 */
RbpPredictOptions *rbp_predict_options_create(void);

/** Free options from ::rbp_predict_options_create. No-op if @p opts is null. */
void rbp_predict_options_free(RbpPredictOptions *opts);

/**
 * Set threshold vector (length @p len ≥ 1). Values are copied.
 * @param values Contiguous doubles of length @p len.
 */
RbpStatus rbp_predict_options_set_threshold(
    RbpPredictOptions *opts, const double *values, size_t len);

/** @param value 0=relevance, 1=similarity (2/Both is invalid for predict). */
RbpStatus rbp_predict_options_set_censor_type(RbpPredictOptions *opts, int32_t value);
/** @param value 0=score, 1=percent */
RbpStatus rbp_predict_options_set_censor_unit(RbpPredictOptions *opts, int32_t value);
/** @param value 0=gt, 1=lt, 2=gte, 3=lte */
RbpStatus rbp_predict_options_set_censor_operator(RbpPredictOptions *opts, int32_t value);
/** @param value 0=response (y scale), 1=logistic */
RbpStatus rbp_predict_options_set_prediction_scale(RbpPredictOptions *opts, int32_t value);
/** @param value 0=identity, 1=K, 2=log */
RbpStatus rbp_predict_options_set_adj_fit_multiplier(RbpPredictOptions *opts, int32_t value);
/** @param value 0=gaussian/LU, 1=cholesky, 2=pseudoinverse */
RbpStatus rbp_predict_options_set_inv_method(RbpPredictOptions *opts, int32_t value);
/** @param value 0=pairwise (default), 1=complete. How μ/Σ/PSR N treat NaNs. */
RbpStatus rbp_predict_options_set_missing_moments(RbpPredictOptions *opts, int32_t value);
/** Deprecated alias: non-zero → complete moments (`set_missing_moments(1)`). */
RbpStatus rbp_predict_options_set_verify_missing_data(RbpPredictOptions *opts, int32_t value);
/** @param value Non-zero attaches linear-regression `yhat_linear` when available. */
RbpStatus rbp_predict_options_set_include_linear_regression(RbpPredictOptions *opts, int32_t value);
/** @param value Non-zero enables diagnostic status messages (default off). */
RbpStatus rbp_predict_options_set_verbose(RbpPredictOptions *opts, int32_t value);

/**
 * UTF-8 string forms of the matching int setters (case-insensitive where noted).
 * Accepted values: see file overview "Option string forms".
 */
RbpStatus rbp_predict_options_set_censor_type_str(RbpPredictOptions *opts, const char *value);
RbpStatus rbp_predict_options_set_censor_unit_str(RbpPredictOptions *opts, const char *value);
RbpStatus rbp_predict_options_set_censor_operator_str(RbpPredictOptions *opts, const char *value);

/* =========================================================================
 * Predict
 * ========================================================================= */

/**
 * Run RBP predict.
 *
 * @param y           Outcomes, length @p n
 * @param n           Number of observations (must equal @p n_rows)
 * @param x           Attribute matrix, logical @p n_rows × @p n_cols
 * @param n_rows      Rows of X (= @p n)
 * @param n_cols      Columns of X (= @p k)
 * @param x_layout    ::RbpLayout for @p x
 * @param theta       Circumstance vector, length @p k
 * @param k           Must equal @p n_cols
 * @param options     Optional; null → defaults (::rbp_predict_options_create defaults)
 * @param out_results On success, receives owned ::RbpPredictionResults*
 *                    (free with ::rbp_prediction_results_free). Set to null on failure.
 * @return ::RBP_OK or an error status.
 */
RbpStatus rbp_predict(
    const double *y,
    size_t n,
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const RbpPredictOptions *options,
    RbpPredictionResults **out_results);

/* =========================================================================
 * MaxFit options
 * ========================================================================= */

/**
 * Allocate MaxFit options (default thresholds `[0,0.2,0.5,0.8]`, censor Both,
 * objective `"kfit"`, inner_parallel auto).
 * @return Owned handle; free with ::rbp_maxfit_options_free.
 */
RbpMaxFitOptions *rbp_maxfit_options_create(void);

/** Free MaxFit options. No-op if @p opts is null. */
void rbp_maxfit_options_free(RbpMaxFitOptions *opts);

/**
 * Objective used to pick the winning threshold: `"fit"`, `"adjusted_fit"`, or `"kfit"`.
 */
RbpStatus rbp_maxfit_options_set_objective(RbpMaxFitOptions *opts, const char *value);

RbpStatus rbp_maxfit_options_set_threshold(
    RbpMaxFitOptions *opts, const double *values, size_t len);
/** @param value 0=relevance, 1=similarity, 2=both */
RbpStatus rbp_maxfit_options_set_censor_type(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_censor_unit(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_censor_operator(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_prediction_scale(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_adj_fit_multiplier(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_inv_method(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_missing_moments(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_verify_missing_data(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_include_linear_regression(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_verbose(RbpMaxFitOptions *opts, int32_t value);
RbpStatus rbp_maxfit_options_set_censor_type_str(RbpMaxFitOptions *opts, const char *value);
/**
 * Within-call parallel autopick for MaxFit (over censor types when Both).
 * Orthogonal to multi-job concurrency at the client layer.
 * @param value 0=auto (default), 1=off (always sequential path)
 */
RbpStatus rbp_maxfit_options_set_inner_parallel(RbpMaxFitOptions *opts, int32_t value);
/** @param value `"auto"` or `"off"` (and other off aliases; see file overview) */
RbpStatus rbp_maxfit_options_set_inner_parallel_str(RbpMaxFitOptions *opts, const char *value);

/* =========================================================================
 * MaxFit
 * ========================================================================= */

/**
 * Run MaxFit (auto parallel when censor type is Both and inner_parallel=auto).
 * Results on the handle are sliced to the single winning threshold (T=1);
 * original search markers may remain in maxfit_index when present.
 *
 * Argument contract matches ::rbp_predict (y/X/theta/layout/options/out_results).
 */
RbpStatus rbp_maxfit(
    const double *y,
    size_t n,
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const RbpMaxFitOptions *options,
    RbpPredictionResults **out_results);

/* =========================================================================
 * Grid options
 * ========================================================================= */

/**
 * Allocate Grid options (default max_iter=1000, k=1, seed=42, retain none,
 * censor Both, thresholds `[0,0.2,0.5,0.8]`, inner_parallel auto).
 */
RbpGridOptions *rbp_grid_options_create(void);

/** Free Grid options. No-op if @p opts is null. */
void rbp_grid_options_free(RbpGridOptions *opts);

/** @param value Must be > 0. Caps combination search iterations. */
RbpStatus rbp_grid_options_set_max_iter(RbpGridOptions *opts, size_t value);
/** @param value Must be > 0. Combination size / sampling parameter. */
RbpStatus rbp_grid_options_set_k(RbpGridOptions *opts, size_t value);
/** RNG seed for combination sampling. */
RbpStatus rbp_grid_options_set_seed(RbpGridOptions *opts, uint32_t value);

/**
 * Retain all per-cell grid objects (non-zero) or none (0).
 * Shorthand for the full retain-key set when true.
 */
RbpStatus rbp_grid_options_set_retain_all(RbpGridOptions *opts, int32_t value);

/**
 * Retain policy string: `"true"`/`"all"`, `"false"`/`"none"`, or comma-separated
 * keys (`yhat_cells`, `weights_cells`, `ysolo_distribution`, …). See file
 * overview for the full key list and bracket/list syntax.
 */
RbpStatus rbp_grid_options_set_retain_grid_objects_str(RbpGridOptions *opts, const char *value);

/**
 * Optional attribute-combination matrix (logical @p n_rows × @p n_cols), copied.
 * @param layout ::RbpLayout for @p data
 */
RbpStatus rbp_grid_options_set_attribute_combi(
    RbpGridOptions *opts,
    const double *data,
    size_t n_rows,
    size_t n_cols,
    int32_t layout);

RbpStatus rbp_grid_options_set_threshold(
    RbpGridOptions *opts, const double *values, size_t len);
/** @param value 0=relevance, 1=similarity, 2=both */
RbpStatus rbp_grid_options_set_censor_type(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_censor_unit(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_censor_operator(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_prediction_scale(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_adj_fit_multiplier(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_inv_method(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_include_linear_regression(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_verbose(RbpGridOptions *opts, int32_t value);
RbpStatus rbp_grid_options_set_censor_type_str(RbpGridOptions *opts, const char *value);
/**
 * Within-call parallel autopick for Grid (over attribute combinations).
 * Autopick size gates when value is auto: Both and N≥250, or single censor
 * and N≥5000; otherwise sequential. Off always forces sequential.
 * @param value 0=auto (default), 1=off
 */
RbpStatus rbp_grid_options_set_inner_parallel(RbpGridOptions *opts, int32_t value);
/** @param value `"auto"` or `"off"` (and other off aliases; see file overview) */
RbpStatus rbp_grid_options_set_inner_parallel_str(RbpGridOptions *opts, const char *value);
/**
 * Subtract 0/1-garbage IOF/IOP for columns with missing values (default on).
 * @param value non-zero = on, 0 = off (pre-adjustment baseline)
 */
RbpStatus rbp_grid_options_set_adjust_missing_importance(RbpGridOptions *opts, int32_t value);
/**
 * Subtract 0/1-garbage IOF/IOP for columns with missing values (default on).
 * @param value non-zero = on, 0 = off (pre-adjustment baseline / PSR parity)
 */
RbpStatus rbp_grid_options_set_adjust_missing_importance(RbpGridOptions *opts, int32_t value);

/* =========================================================================
 * Grid
 * ========================================================================= */

/**
 * Run grid search (parallel when inner_parallel=auto and size gates pass:
 * Both and N≥250, or single censor and N≥5000).
 *
 * On success, @p out_results holds details including composite ``yhat`` of length 1
 * (copy with ::rbp_results_copy_yhat).
 * Grid insights (MCTC/MCTP/…) are typically filled even when retain is off;
 * per-combination cell tensors require retain options.
 *
 * Argument contract matches ::rbp_predict.
 */
RbpStatus rbp_grid(
    const double *y,
    size_t n,
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const RbpGridOptions *options,
    RbpPredictionResults **out_results);

/* =========================================================================
 * Insights (standalone — no predict call required)
 * =========================================================================
 *
 * All take training matrix X (N×K). μ is the column mean of X.
 * @p cov_inv may be NULL (inverse covariance computed from X); when NULL,
 * @p cov_inv_layout is ignored. When non-null, cov_inv is K×K in the given layout.
 *
 * Output lengths:
 *   relevance / similarity / info_x → N
 *   info_theta → 1  (Mahalanobis-style informativeness of θ vs μ)
 */

/**
 * Relevance of each training row to circumstance @p theta (length N).
 * @param out Caller buffer; @p out_len must be ≥ @p n_rows
 */
RbpStatus rbp_relevance(
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const double *cov_inv,
    int32_t cov_inv_layout,
    double *out,
    size_t out_len);

/**
 * Similarity of each training row to @p theta (length N): −½ Mahalanobis(X, θ).
 */
RbpStatus rbp_similarity(
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const double *cov_inv,
    int32_t cov_inv_layout,
    double *out,
    size_t out_len);

/**
 * Informativeness of each training observation vs the sample mean (length N).
 * Does not use θ; only X (and optional cov_inv) are required.
 */
RbpStatus rbp_info_x(
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *cov_inv,
    int32_t cov_inv_layout,
    double *out,
    size_t out_len);

/**
 * Informativeness of circumstances @p theta vs the training-sample mean (length 1).
 * @param out_len Must be ≥ 1
 */
RbpStatus rbp_info_theta(
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const double *cov_inv,
    int32_t cov_inv_layout,
    double *out,
    size_t out_len);

/**
 * Compute relevance once and optionally fill all four score buffers.
 * Any `out_*` pointer may be NULL to skip that output. Non-null buffers must
 * satisfy the length rules above (N / N / N / 1).
 */
RbpStatus rbp_relevance_metrics(
    const double *x,
    size_t n_rows,
    size_t n_cols,
    int32_t x_layout,
    const double *theta,
    size_t k,
    const double *cov_inv,
    int32_t cov_inv_layout,
    double *out_relevance,
    size_t out_relevance_len,
    double *out_similarity,
    size_t out_similarity_len,
    double *out_info_x,
    size_t out_info_x_len,
    double *out_info_theta,
    size_t out_info_theta_len);

/* =========================================================================
 * Results
 * ========================================================================= */

/**
 * Free a results handle from predict / maxfit / grid. No-op if null.
 * Invalidates the pointer; do not free twice or use after free.
 */
void rbp_prediction_results_free(RbpPredictionResults *results);

/** @return N, or 0 if @p results is null. */
size_t rbp_results_num_observations(const RbpPredictionResults *results);
/** @return K, or 0 if @p results is null. */
size_t rbp_results_num_variables(const RbpPredictionResults *results);
/**
 * @return T (number of thresholds retained on this handle).
 * MaxFit collapses to T=1 (winning threshold). Grid composite is typically T=1.
 */
size_t rbp_results_num_thresholds(const RbpPredictionResults *results);

/**
 * Copy core threshold-indexed vectors into caller buffers.
 * Each is length T; @p len must be ≥ ::rbp_results_num_thresholds(@p results).
 * Always available after a successful predict / maxfit / grid.
 */
RbpStatus rbp_results_copy_thresholds(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_yhat(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_fit(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_adjusted_fit(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_agreement(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_asymmetry(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_k_fit(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_outlier_influence(const RbpPredictionResults *results, double *out, size_t len);

/** @return 1 if linear `yhat_linear` is present, else 0. */
int32_t rbp_results_has_yhat_linear(const RbpPredictionResults *results);
/**
 * Copy `yhat_linear` when present (length T).
 * @return ::RBP_ERR_INVALID_ARG if not available (enable include_linear_regression).
 */
RbpStatus rbp_results_copy_yhat_linear(const RbpPredictionResults *results, double *out, size_t len);

/**
 * Copy insight vectors populated by predict / maxfit (when stored on the handle).
 * Lengths: relevance / similarity / info_x → N; info_theta → 1.
 * @return ::RBP_ERR_INVALID_ARG if that insight was not stored on @p results.
 */
RbpStatus rbp_results_copy_relevance(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_similarity(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_info_x(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_info_theta(const RbpPredictionResults *results, double *out, size_t len);

/* ----- Optional depth (lean by default; filled by predict / maxfit / retain) ----- */

/** Grid cell nest: pass as @p censor on has_/dims/copy for per-censor cells. */
#define RBP_GRID_CENSOR_RELEVANCE 0
#define RBP_GRID_CENSOR_SIMILARITY 1

/**
 * Weights on included observations (typically N×T). Use dims then allocate
 * `rows*cols`. Prefer ::RBP_LAYOUT_ROW_MAJOR for C / NumPy consumers.
 */
int32_t rbp_results_has_weights(const RbpPredictionResults *results);
RbpStatus rbp_results_weights_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_weights(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);

/** Weights on excluded side (same shape pattern as weights when present). */
int32_t rbp_results_has_weights_excluded(const RbpPredictionResults *results);
RbpStatus rbp_results_weights_excluded_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_weights_excluded(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);

/** Include mask (typically N×T). @p out is uint8 0/1; layout as other 2-D copies. */
int32_t rbp_results_has_include(const RbpPredictionResults *results);
RbpStatus rbp_results_include_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
/** Copy include mask as uint8 0/1. @p len ≥ rows*cols. */
RbpStatus rbp_results_copy_include(const RbpPredictionResults *results, uint8_t *out, size_t len, int32_t layout);

/**
 * Per-threshold auxiliary diagnostics (length T each when present).
 * Phi / lambda_sq / full_var / part_var / r_star / r_star_percent / rho
 * are f64; aux n and k are integer counts stored as f64.
 */
int32_t rbp_results_has_auxiliary(const RbpPredictionResults *results);
RbpStatus rbp_results_copy_aux_phi(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_lambda_sq(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_full_var(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_part_var(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_r_star(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_r_star_percent(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_rho(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_n(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_aux_k(const RbpPredictionResults *results, double *out, size_t len);

/** Length T when present. */
RbpStatus rbp_results_copy_weights_concentration(const RbpPredictionResults *results, double *out, size_t len);

/** MaxFit search-path index over original thresholds (length may exceed result T=1). */
int32_t rbp_results_has_maxfit_index(const RbpPredictionResults *results);
/** @return Length of maxfit_index (original search T), or 0 if absent. */
size_t rbp_results_maxfit_index_len(const RbpPredictionResults *results);
RbpStatus rbp_results_copy_maxfit_index(const RbpPredictionResults *results, double *out, size_t len);

/** Leave-one / solo outcomes (y_solo length N when present). */
int32_t rbp_results_has_y_solo(const RbpPredictionResults *results);
RbpStatus rbp_results_copy_y_solo(const RbpPredictionResults *results, double *out, size_t len);
int32_t rbp_results_has_xi_solo(const RbpPredictionResults *results);
RbpStatus rbp_results_xi_solo_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_xi_solo(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);
/** Solo summary stats (length typically T; @p len must be ≥ vector length). */
RbpStatus rbp_results_copy_ysolo_sigma(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_skewness(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_kurtosis(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_pearson_modality_index(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_bimodal_index(const RbpPredictionResults *results, double *out, size_t len);

/**
 * Optional IQR-filtered y(solo) histogram. dims → (n_bins, n_cols) for counts.
 * Buffer sizes: bin_edges length n_bins+1; bin_centers and bin_widths length
 * n_bins; bin_counts is 2-D n_bins × n_cols (layout applies).
 */
int32_t rbp_results_has_ysolo_distribution(const RbpPredictionResults *results);
RbpStatus rbp_results_ysolo_distribution_dims(const RbpPredictionResults *results, size_t *out_n_bins, size_t *out_n_cols);
RbpStatus rbp_results_copy_ysolo_bin_edges(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_bin_centers(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_bin_widths(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_ysolo_bin_counts(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);

/**
 * Grid insights (typically filled on successful grid).
 * Lengths: variable_weights, mctc, mctp, cctp → K; xi_solo_composite → N.
 * Semantics: MCTC = marginal contribution to conviction; MCTP = prediction;
 * CCTP = component contribution to prediction.
 * xi_solo_composite is omitted on lean Grid (copy returns invalid-arg);
 * retain key ysolo_distribution computes and fills it.
 */
int32_t rbp_results_has_grid_insights(const RbpPredictionResults *results);
RbpStatus rbp_results_copy_variable_weights(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_mctc(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_mctp(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_cctp(const RbpPredictionResults *results, double *out, size_t len);
RbpStatus rbp_results_copy_xi_solo_composite(const RbpPredictionResults *results, double *out, size_t len);

/** Grid cell tensors when retain options requested storage. */
int32_t rbp_results_has_grid_cells(const RbpPredictionResults *results);
/** @return Q (combination count) when grid cells exist, else 0. */
size_t rbp_results_num_combinations(const RbpPredictionResults *results);

/** Shared cell tables: k_cells (Q×T), combi_cells (Q×K), ysolo_cells (N×Q). */
int32_t rbp_results_has_k_cells(const RbpPredictionResults *results);
RbpStatus rbp_results_k_cells_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_k_cells(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);
int32_t rbp_results_has_combi_cells(const RbpPredictionResults *results);
RbpStatus rbp_results_combi_cells_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_combi_cells(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);
int32_t rbp_results_has_ysolo_cells(const RbpPredictionResults *results);
RbpStatus rbp_results_ysolo_cells_dims(const RbpPredictionResults *results, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_ysolo_cells(const RbpPredictionResults *results, double *out, size_t len, int32_t layout);

/**
 * Per-censor retained cells. @p censor is ::RBP_GRID_CENSOR_RELEVANCE or
 * ::RBP_GRID_CENSOR_SIMILARITY. yhat / adjusted_fit / n cells are 2-D (Q×T).
 */
int32_t rbp_results_has_yhat_cells(const RbpPredictionResults *results, int32_t censor);
RbpStatus rbp_results_yhat_cells_dims(const RbpPredictionResults *results, int32_t censor, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_yhat_cells(const RbpPredictionResults *results, int32_t censor, double *out, size_t len, int32_t layout);
int32_t rbp_results_has_adjusted_fit_cells(const RbpPredictionResults *results, int32_t censor);
RbpStatus rbp_results_adjusted_fit_cells_dims(const RbpPredictionResults *results, int32_t censor, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_adjusted_fit_cells(const RbpPredictionResults *results, int32_t censor, double *out, size_t len, int32_t layout);
int32_t rbp_results_has_n_cells(const RbpPredictionResults *results, int32_t censor);
RbpStatus rbp_results_n_cells_dims(const RbpPredictionResults *results, int32_t censor, size_t *out_rows, size_t *out_cols);
RbpStatus rbp_results_copy_n_cells(const RbpPredictionResults *results, int32_t censor, double *out, size_t len, int32_t layout);

/**
 * 3-D cell tensors: shape (d0,d1,d2) ≈ (N,T,Q). Flat C-order layout only:
 * index i0*(d1*d2)+i1*d2+i2. @p len ≥ product of dims.
 */
int32_t rbp_results_has_weights_cells(const RbpPredictionResults *results, int32_t censor);
RbpStatus rbp_results_weights_cells_dims(const RbpPredictionResults *results, int32_t censor, size_t *out_d0, size_t *out_d1, size_t *out_d2);
RbpStatus rbp_results_copy_weights_cells(const RbpPredictionResults *results, int32_t censor, double *out, size_t len);
int32_t rbp_results_has_xi_solo_cells(const RbpPredictionResults *results, int32_t censor);
RbpStatus rbp_results_xi_solo_cells_dims(const RbpPredictionResults *results, int32_t censor, size_t *out_d0, size_t *out_d1, size_t *out_d2);
RbpStatus rbp_results_copy_xi_solo_cells(const RbpPredictionResults *results, int32_t censor, double *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RBP_MATH_H */
