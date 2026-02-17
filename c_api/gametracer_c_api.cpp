#include "gametracer_c_api.h"

#include "cmatrix.h"
#include "gnm.h"
#include "ipa.h"
#include "nfgame.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#include <exception>

namespace {

struct GameSizes {
    int N;            // num_players
    int M;            // sum(actions)
    int P;            // prod(actions)
    int payoff_len;   // N * P
};

static bool compute_sizes(int num_players, const int* actions, GameSizes& out) {
    if (num_players <= 0 || actions == nullptr) return false;

    size_t M = 0;
    size_t P = 1;

    for (int p = 0; p < num_players; ++p) {
        int ap = actions[p];
        if (ap <= 0) return false;

        M += static_cast<size_t>(ap);

        if (P > std::numeric_limits<size_t>::max() / static_cast<size_t>(ap)) return false;
        P *= static_cast<size_t>(ap);
    }

    if (M > static_cast<size_t>(INT_MAX)) return false;
    if (P > static_cast<size_t>(INT_MAX)) return false;

    size_t payoff_len = static_cast<size_t>(num_players) * P;
    if (payoff_len > static_cast<size_t>(INT_MAX)) return false;

    out.N = num_players;
    out.M = static_cast<int>(M);
    out.P = static_cast<int>(P);
    out.payoff_len = static_cast<int>(payoff_len);
    return true;
}

static void cleanup_eq(cvector** Eq, int numEq) {
    if (!Eq) return;
    for (int k = 0; k < numEq; ++k) {
        delete Eq[k];
    }
    std::free(Eq);
}

} // namespace

extern "C" {

GAMETRACER_API void GAMETRACER_CALL gametracer_free(void* p) {
    std::free(p);
}

GAMETRACER_API int GAMETRACER_CALL ipa(
    int num_players,
    const int* actions,
    const double* payoffs,
    const double* g,
    double* zh,
    double alpha,
    double fuzz,
    double* ans
) {
    if (actions == nullptr || payoffs == nullptr || g == nullptr || zh == nullptr || ans == nullptr)
        return -1;

    GameSizes sz;
    if (!compute_sizes(num_players, actions, sz))
        return -1;

    try {
        std::vector<int> acts(static_cast<size_t>(sz.N));
        for (int p = 0; p < sz.N; ++p) acts[p] = actions[p];

        cvector payvec(sz.payoff_len);
        std::memcpy(payvec.values(), payoffs, static_cast<size_t>(sz.payoff_len) * sizeof(double));

        nfgame A(sz.N, acts.data(), payvec);

        cvector gvec(sz.M);
        std::memcpy(gvec.values(), g, static_cast<size_t>(sz.M) * sizeof(double));

        cvector zhvec(sz.M);
        std::memcpy(zhvec.values(), zh, static_cast<size_t>(sz.M) * sizeof(double));

        cvector ansvec(sz.M);

        int ret = IPA(A, gvec, zhvec, alpha, fuzz, ansvec);

        // Copy back outputs
        std::memcpy(zh, zhvec.values(), static_cast<size_t>(sz.M) * sizeof(double));
        std::memcpy(ans, ansvec.values(), static_cast<size_t>(sz.M) * sizeof(double));

        return ret;
    } catch (const std::bad_alloc&) {
        return -2;
    } catch (...) {
        return -3;
    }
}

GAMETRACER_API int GAMETRACER_CALL gnm(
    int num_players,
    const int* actions,
    const double* payoffs,
    const double* g,
    double** answers,
    int steps,
    double fuzz,
    int lnmfreq,
    int lnmmax,
    double lambdamin,
    int wobble,
    double threshold
) {
    if (answers) *answers = nullptr;

    if (actions == nullptr || payoffs == nullptr || g == nullptr || answers == nullptr)
        return -1;

    GameSizes sz;
    if (!compute_sizes(num_players, actions, sz))
        return -1;

    cvector** Eq = nullptr;
    int found = 0;          // hoisted for exception-safe cleanup
    double* buf = nullptr;  // in case we allocate and then throw

    try {
        std::vector<int> acts(static_cast<size_t>(sz.N));
        for (int p = 0; p < sz.N; ++p) acts[p] = actions[p];

        cvector payvec(sz.payoff_len);
        std::memcpy(payvec.values(), payoffs, static_cast<size_t>(sz.payoff_len) * sizeof(double));

        nfgame A(sz.N, acts.data(), payvec);

        // Treat g as immutable: copy into local cvector before calling upstream GNM (which mutates g)
        cvector gvec(sz.M);
        std::memcpy(gvec.values(), g, static_cast<size_t>(sz.M) * sizeof(double));

        found = GNM(A, gvec, Eq, steps, fuzz, lnmfreq, lnmmax, lambdamin, wobble, threshold);

        if (found == 0) {
            cleanup_eq(Eq, 0);
            *answers = nullptr;
            return 0;
        }
        if (found < 0) {
            // Upstream should not return <0, but treat it as internal error if it happens.
            cleanup_eq(Eq, 0);
            *answers = nullptr;
            return -3;
        }

        // Allocate contiguous output buffer: found * M doubles
        size_t total = static_cast<size_t>(found) * static_cast<size_t>(sz.M);
        buf = static_cast<double*>(std::malloc(total * sizeof(double)));
        if (!buf) {
            cleanup_eq(Eq, found);
            Eq = nullptr;
            return -2;
        }

        for (int k = 0; k < found; ++k) {
            std::memcpy(buf + static_cast<size_t>(k) * static_cast<size_t>(sz.M),
                        Eq[k]->values(),
                        static_cast<size_t>(sz.M) * sizeof(double));
        }

        cleanup_eq(Eq, found);
        Eq = nullptr;

        *answers = buf;
        buf = nullptr; // ownership transferred to caller
        return found;

    } catch (const std::bad_alloc&) {
        if (buf) std::free(buf);
        cleanup_eq(Eq, (found > 0) ? found : 0);
        *answers = nullptr;
        return -2;
    } catch (...) {
        if (buf) std::free(buf);
        cleanup_eq(Eq, (found > 0) ? found : 0);
        *answers = nullptr;
        return -3;
    }
}

} // extern "C"
