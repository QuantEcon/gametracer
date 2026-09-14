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
- Inputs: game (num_players, actions, payoffs), g (length M), alpha, fuzz,
  max_iter, max_pivots
- zh is an in/out work buffer of length M (mutated)
- ans is output buffer of length M; on return values 1 and 0 it holds
  the equilibrium and the last iterate, respectively; on negative return
  values it is unchanged
- max_iter: maximum number of iterations (polymatrix approximations), >= 1
- max_pivots: maximum number of pivoting steps along the Lemke-Howson path
  in each solve of a polymatrix approximation (the entry pivot included,
  the basis setup pivots excluded), >= 1
- num_iter: if not NULL, receives the number of iterations performed
Return value:
- 1 : success; ans holds an equilibrium
- 0 : no equilibrium found; either max_iter was reached or the solver
      gave up (singular support system, Lemke-Howson ray termination, or
      max_pivots reached); in both cases ans holds the last iterate, a
      valid mixed action profile. Reaching max_iter implies
      *num_iter == max_iter; the converse need not hold, since the solver
      can give up during the last allowed iteration
- <0: shim-detected error:
    -1 invalid args (null pointer, num_players < 2, actions[p] <= 0,
       max_iter < 1, max_pivots < 1) / size overflow
       (including M + num_players + 2 > 46340, the largest size whose
       square fits in int, as required by the core matrix code)
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
    double* ans,                  /* length M (output) */
    int max_iter,                 /* maximum number of iterations, >= 1 */
    int max_pivots,               /* maximum pivots per Lemke-Howson solve, >= 1 */
    int* num_iter                 /* output: iterations performed (may be NULL) */
);

/*
gnm:
- Inputs: game (num_players, actions, payoffs), g (length M), algorithm params
- g is treated as immutable by the shim (copied before calling upstream GNM)
- max_iter: maximum number of iterations, >= 1, where an iteration is the
  traversal of one support cell; if reached, the equilibria found so far
  are returned
- num_iter: if not NULL, receives the number of iterations performed, i.e.
  of support cells entered (the last one may be left before its boundary
  is crossed, so the count is a measure of work, not of crossings)
- Output:
    *answers = malloc'd buffer of length (ret * M) doubles, or NULL if ret == 0
              layout: answers[k*M + i] is i-th entry of equilibrium k
Return value:
- >=0: number of equilibria found
- <0 : shim-detected error:
    -1 invalid args (null pointer, num_players < 2, actions[p] <= 0,
       max_iter < 1) / size overflow
       (including M + num_players + 2 > 46340, the largest size whose
       square fits in int, as required by the core matrix code)
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
    double threshold,
    int max_iter,                 /* maximum number of iterations, >= 1 */
    int* num_iter                 /* output: iterations performed (may be NULL) */
);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GAMETRACER_C_API_H */
