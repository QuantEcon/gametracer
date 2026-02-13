#ifndef GAMETRACER_C_API_H
#define GAMETRACER_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(GAMETRACER_BUILDING_DLL)
    #define GAMETRACER_API __declspec(dllexport)
  #else
    #define GAMETRACER_API __declspec(dllimport)
  #endif
  #define GAMETRACER_CALL __cdecl
#else
  #if defined(__GNUC__) && (__GNUC__ >= 4)
    #define GAMETRACER_API __attribute__((visibility("default")))
  #else
    #define GAMETRACER_API
  #endif
  #define GAMETRACER_CALL
#endif

/*
Layout contract (matches upstream nfgame):
- num_players = N
- actions[p] = a_p, length N
- P = prod_p a_p
- M = sum_p a_p

payoffs: length N*P (player-major blocks)
  payoffs[p*P + profile_index(s)] where pure profile s = (s0,...,s_{N-1}) and
  profile_index(s) = s0 + s1*a0 + s2*a0*a1 + ...

Flattened action vectors g/zh/ans: length M (player-concatenated)
  offset[p] = sum_{k<p} a_k
  entry offset[p] + j corresponds to player p action j
*/

/* Free buffers allocated by the library (e.g. gnm() answers). Safe on NULL. */
GAMETRACER_API void GAMETRACER_CALL gametracer_free(void* p);

/*
ipa:
- Inputs: game (num_players, actions, payoffs), g (length M), alpha, fuzz
- zh is an in/out work buffer of length M (mutated)
- ans is output buffer of length M (filled on success)
Return value:
- >0: success (passes through upstream IPA return)
- 0 : failure/no equilibrium found (upstream convention)
- <0: shim-detected error:
    -1 invalid args / size overflow
    -2 allocation failure
    -3 exception/internal
*/
GAMETRACER_API int GAMETRACER_CALL ipa(
    int num_players,
    const int* actions,           /* length num_players */
    const double* payoffs,        /* length num_players * prod(actions) */
    const double* g,              /* length M */
    double* zh,                   /* length M (in/out work buffer) */
    double alpha,
    double fuzz,                  /* equilibrium cutoff tolerance */
    double* ans                   /* length M (output) */
);

/*
gnm:
- Inputs: game (num_players, actions, payoffs), g (length M), algorithm params
- g is treated as immutable by the shim (copied before calling upstream GNM)
- Output:
    *answers = malloc'd buffer of length (ret * M) doubles, or NULL if ret == 0
              layout: answers[k*M + i] is i-th entry of equilibrium k
Return value:
- >=0: number of equilibria found
- <0 : shim-detected error:
    -1 invalid args / size overflow
    -2 allocation failure
    -3 exception/internal
Caller must free *answers with gametracer_free (safe on NULL).
*/
GAMETRACER_API int GAMETRACER_CALL gnm(
    int num_players,
    const int* actions,           /* length num_players */
    const double* payoffs,        /* length num_players * prod(actions) */
    const double* g,              /* length M (treated as immutable by shim) */
    double** answers,             /* output: malloc'd; free with gametracer_free */
    int steps,
    double fuzz,
    int lnmfreq,
    int lnmmax,
    double lambdamin,
    int wobble,
    double threshold
);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GAMETRACER_C_API_H */
