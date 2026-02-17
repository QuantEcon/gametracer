# GameTracer C ABI

This directory provides a stable C ABI shim over the `GameTracer` C++ codebase.
It builds a shared library `libgametracer`,
and its primary intended use is via `BinaryBuilder` and for access from Julia with `ccall`.

## Exports

- `ipa`
- `gnm`
- `gametracer_free`

The shim ensures:
- no C++ exceptions cross the ABI boundary (errors are reported via return codes);
- explicit memory ownership rules for returned buffers (freed via `gametracer_free`).

## Local build and install

One can also build and install the shim locally for development or direct `ccall` testing.

From the **repository root** (assuming the destination is `/tmp/gametracer_install`):

```sh
cmake -S c_api -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/tmp/gametracer_install
cmake --build build --config Release
cmake --install build --config Release
```

For clean rebuild:

```sh
rm -rf build /tmp/gametracer_install
```

Install layout:

- Header: `/tmp/gametracer_install/include/gametracer_c_api.h`
- Library:
  - macOS/Linux: `/tmp/gametracer_install/lib/libgametracer.{dylib,so}`
  - Windows: `/tmp/gametracer_install/bin/libgametracer.dll`
- License: `/tmp/gametracer_install/share/licenses/gametracer/COPYING`

## Example use in Julia with `ccall`

```julia
using Libdl

# Suppose the shared library has been installed to "/tmp/gametracer_install"
const prefix  = "/tmp/gametracer_install"
const LIBPATH = joinpath(prefix, Sys.iswindows() ? "bin" : "lib",
                         "libgametracer.$(Libdl.dlext)")

# Example 3x2 game from von Stengel (2007)
#  [3.0, 3.0]  [3.0, 2.0]
#  [2.0, 2.0]  [5.0, 6.0]
#  [0.0, 3.0]  [6.0, 1.0]
# with 3 equilibria
#  ([1, 0, 0], [1, 0])
#  ([4/5, 1/5, 0], [2/3, 1/3])
#  ([0, 1/3, 2/3], [1/3, 2/3])
N = 2  # Number of players
actions = Cint[3, 2]  # Numbers of actions of each player
M = sum(actions)  # Total number of actions
P = prod(actions)  # Number of action profiles

# Payoffs are stored linearly as player-major blocks;
# Pure-action profiles are ordered with first player varying fastest.
payoffs = [
    3.0, 2.0, 0.0, 3.0, 5.0, 6.0,  # For player 1
    3.0, 2.0, 3.0, 2.0, 6.0, 1.0   # For player 2
]

# --- IPA ---

# Perturbation ray; length M
ray = [0.0, 0.0, 1.0, 0.0, 1.0]

# Initial (flattened) mixed-action profile (to be modified); length M
init = [1/3, 1/3, 1/3, 1/2, 1/2]

# Output (flattened) mixed-action profile; length M
out = Vector{Float64}(undef, M)

# Parameters
alpha = 0.02
fuzz = 1e-6

ret_ipa = ccall((:ipa, LIBPATH), Cint,
    (Cint, Ptr{Cint}, Ptr{Cdouble},
     Ptr{Cdouble}, Ptr{Cdouble},
     Cdouble, Cdouble,
     Ptr{Cdouble}),
    N, actions, payoffs,
    ray, init,
    alpha, fuzz,
    out)

ret_ipa > 0 || error("ipa failed (ret = $ret_ipa)")

println("IPA")
println(out)  # Flattened mixed-action profile

# --- GNM ---

# Perturbation ray; length M
ray = [0.0, 0.0, 1.0, 0.0, 1.0]

# Out parameter (`double** answers`): receives a `double*` buffer (malloc'd);
# free via `gametracer_free`
answers_ref = Ref{Ptr{Cdouble}}(C_NULL)

# Parameters
steps = 100
fuzz = 1e-12
lnmfreq = 3
lnmmax = 10
lambdamin = -10.0
wobble = 0
threshold = 1e-2

ret_gnm = ccall((:gnm, LIBPATH), Cint,
    (Cint, Ptr{Cint}, Ptr{Cdouble},
     Ptr{Cdouble}, Ref{Ptr{Cdouble}},
     Cint, Cdouble, Cint, Cint, Cdouble, Cint, Cdouble),
    N, actions, payoffs,
    ray, answers_ref,
    steps, fuzz, lnmfreq, lnmmax, lambdamin, wobble, threshold)

if ret_gnm < 0
    error("gnm failed (ret = $ret_gnm)")
elseif ret_gnm == 0
    println("gnm found 0 equilibria (ret = 0)")
else
    num_eq = Int(ret_gnm)
    ptr = answers_ref[]
    ptr != C_NULL || error("gnm returned num_eq>0 but answers pointer was NULL")

    answers = try
        answers_view = unsafe_wrap(Array, ptr, (M, num_eq); own=false)
        # Copy into Julia-owned memory before freeing the C buffer
        copy(answers_view)
    finally
        # Free the C-allocated buffer returned by `gnm`
        # (use `gametracer_free`, not `Libc.free`)
        ccall((:gametracer_free, LIBPATH), Cvoid, (Ptr{Cvoid},), ptr)
    end

    println("GNM")
    for j in 1:num_eq
        println(answers[:, j])  # Flattened mixed-action profile
    end
end
```

## Return codes

Both `ipa` and `gnm` return an `int` (`Cint` in Julia).
Interpret the return value as follows.

### `ipa`

- `ret > 0` : success
- `ret == 0`: failure / no equilibrium found
- `ret < 0` : error code (see **Error codes** below)

### `gnm`

- `ret > 0` : success; `ret` is the number of equilibria found
  - on success, `*answers` points to a contiguous `malloc`’d buffer of length `num_eq * M`
- `ret == 0`: success; found 0 equilibria
  - in this case, `*answers == NULL`
- `ret < 0` : error code (see **Error codes** below)
  - in this case, `*answers == NULL`

### Error codes (`ret < 0`)

| Code | Meaning |
|---:|---|
| `-1` | **Invalid arguments / size overflow**. E.g., null pointer, `actions[p] <= 0`, overflow of `M`, `P`, or `N*P`. |
| `-2` | **Allocation failure.** `std::bad_alloc` or failed `malloc` (notably, allocating the contiguous `answers` buffer in `gnm`). |
| `-3` | **Internal error / unexpected exception.** Any non-`bad_alloc` exception, or an unexpected negative return from upstream `GNM` (treated as internal error). |
