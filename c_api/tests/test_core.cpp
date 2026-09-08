// Regression tests that call the C++ core directly (the core symbols are not
// exported from the shared library, so this program compiles the core and the
// C API shim in).
//
// Build and run from the repository root:
//   cmake -S c_api -B build -DGAMETRACER_BUILD_TESTS=ON
//   cmake --build build
//   ctest --test-dir build --output-on-failure

#include "cmatrix.h"
#include "gametracer_c_api.h"
#include "gnm.h"
#include "nfgame.h"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// ---------------------------------------------------------------------------
// Counting replacement of the global allocation functions, so that a test can
// inject an allocation failure at a chosen point and check for leaks.
// ---------------------------------------------------------------------------
namespace {
long g_new_count = 0;     // successful allocations so far
long g_delete_count = 0;  // deallocations so far
long g_fail_at = -1;      // fail the allocation with this index (-1: never)

void* counted_new(std::size_t n) {
    if (g_fail_at >= 0 && g_new_count == g_fail_at) {
        g_fail_at = -1;
        throw std::bad_alloc();
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    ++g_new_count;
    return p;
}
void counted_delete(void* p) {
    if (p) {
        ++g_delete_count;
        std::free(p);
    }
}
}  // namespace

void* operator new(std::size_t n) { return counted_new(n); }
void* operator new[](std::size_t n) { return counted_new(n); }
void operator delete(void* p) noexcept { counted_delete(p); }
void operator delete[](void* p) noexcept { counted_delete(p); }
void operator delete(void* p, std::size_t) noexcept { counted_delete(p); }
void operator delete[](void* p, std::size_t) noexcept { counted_delete(p); }

namespace {

int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures; \
        } \
    } while (0)

// nfgame::payoffMatrix used to keep a work array of one double per pure
// action profile on the stack, which overflowed the default stack for large
// games (2^20 profiles here is 8 MB).
void test_payoff_matrix_large_game() {
    const int N = 10;
    std::vector<int> actions(N, 4);  // 4^10 = 2^20 profiles
    int P = 1, M = 0;
    for (int p = 0; p < N; ++p) { P *= actions[p]; M += actions[p]; }

    cvector payoffs(N * P);
    for (int i = 0; i < N * P; ++i) payoffs[i] = (i % 7) * 0.25;
    nfgame game(N, actions.data(), payoffs);

    cvector s(M, 0.25);  // uniform mixed actions
    cmatrix DG(M, M);
    game.payoffMatrix(DG, s, 0.0);
    CHECK(DG.isvalid());

    // Under uniform mixing the expected payoff is the mean of the payoffs
    // (getMixedPayoff used to recurse one player too far and read out of
    // bounds).
    double mean = 0.0;
    for (int i = 0; i < P; ++i) mean += payoffs[i];
    mean /= P;
    CHECK(std::fabs(game.getMixedPayoff(0, s) - mean) < 1e-9);
}

// cmatrix::adjoint used to keep an m-by-m work matrix on the stack. A zero
// matrix still makes adjoint copy the input into that buffer (1100^2 doubles
// is 9.7 MB), but it exits on the singular matrix after inspecting only two
// columns, so the test stays cheap in unoptimized builds.
void test_adjoint_large_matrix() {
    const int m = 1100;
    cmatrix A(m, m, 0.0);
    CHECK(A.adjoint() == DBL_MAX);  // singular
}

// Checks the values computed by adjoint: for
//   A = [1 2 3; 0 1 4; 5 6 0]
// det A = 1 and adj A = inv A = [-24 18 5; 20 -15 -4; -5 4 1].
void test_adjoint_values() {
    cmatrix A(3, 3, 0.0);
    const double a[3][3] = {{1, 2, 3}, {0, 1, 4}, {5, 6, 0}};
    const double adj[3][3] = {{-24, 18, 5}, {20, -15, -4}, {-5, 4, 1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) A[i][j] = a[i][j];
    double det = A.adjoint();
    CHECK(std::fabs(det - 1.0) < 1e-12);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) CHECK(std::fabs(A[i][j] - adj[i][j]) < 1e-12);
}

// 2x2x2 game from McKelvey and McLennan (1996) with 9 Nash equilibria, in
// the C ABI layout; the ray below leads GNM to 6 of them.
struct SmallGame {
    int actions[3];
    double payoffs[24];
    double ray[6];
    SmallGame() {
        actions[0] = actions[1] = actions[2] = 2;
        for (int i = 0; i < 24; ++i) payoffs[i] = 0.0;
        const int prof[4][3] = {{0, 0, 0}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
        const double u[4][3] = {{9, 8, 12}, {9, 8, 2}, {3, 4, 6}, {3, 4, 4}};
        for (int e = 0; e < 4; ++e) {
            int idx = prof[e][0] + 2 * prof[e][1] + 4 * prof[e][2];
            for (int p = 0; p < 3; ++p) payoffs[p * 8 + idx] = u[e][p];
        }
        const double r[6] = {0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
        for (int i = 0; i < 6; ++i) ray[i] = r[i];
    }
};

// When an allocation fails inside GNM after some equilibria have been saved,
// the equilibrium array must still be NULL-terminated so that the caller can
// free the saved equilibria, and the C API must report -2 without leaking.
// Each level (core, C API) is first run normally to count its allocations,
// then run again with the last allocation failing.
void test_gnm_allocation_failure() {
    SmallGame g;

    // Core
    {
        std::vector<int> acts(g.actions, g.actions + 3);
        cvector payvec(g.payoffs, 24);
        nfgame A(3, acts.data(), payvec);
        cvector gvec(g.ray, 6);
        cvector** Eq = NULL;

        long outstanding = g_new_count - g_delete_count;
        long new_before = g_new_count;
        int ret = GNM(A, gvec, Eq, 100, 1e-12, 3, 10, -10.0, 0, 1e-2);
        CHECK(ret >= 2);
        int saved = 0;
        for (; Eq[saved] != NULL; ++saved) {}
        CHECK(saved == ret);
        for (int k = 0; k < saved; ++k) delete Eq[k];
        std::free(Eq);
        CHECK(g_new_count - g_delete_count == outstanding);
        long total = g_new_count - new_before;
        CHECK(total > 0);

        gvec = cvector(g.ray, 6);  // GNM scales g
        Eq = NULL;
        g_fail_at = g_new_count + total - 1;
        bool threw = false;
        try {
            GNM(A, gvec, Eq, 100, 1e-12, 3, 10, -10.0, 0, 1e-2);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        g_fail_at = -1;
        CHECK(threw);
        CHECK(Eq != NULL);
        saved = 0;
        if (Eq) {
            for (; Eq[saved] != NULL; ++saved) {}
            for (int k = 0; k < saved; ++k) delete Eq[k];
            std::free(Eq);
        }
        CHECK(saved >= 1);
        CHECK(g_new_count - g_delete_count == outstanding);
    }

    // C API
    {
        double* answers = NULL;
        long outstanding = g_new_count - g_delete_count;
        long new_before = g_new_count;
        int ret = gnm(3, g.actions, g.payoffs, g.ray, &answers,
                      100, 1e-12, 3, 10, -10.0, 0, 1e-2);
        CHECK(ret >= 2);
        gametracer_free(answers);
        CHECK(g_new_count - g_delete_count == outstanding);
        long total = g_new_count - new_before;

        answers = reinterpret_cast<double*>(0x1);
        g_fail_at = g_new_count + total - 1;
        ret = gnm(3, g.actions, g.payoffs, g.ray, &answers,
                  100, 1e-12, 3, 10, -10.0, 0, 1e-2);
        g_fail_at = -1;
        CHECK(ret == -2);
        CHECK(answers == NULL);
        CHECK(g_new_count - g_delete_count == outstanding);
    }
}

}  // namespace

int main() {
    test_payoff_matrix_large_game();
    test_adjoint_large_matrix();
    test_adjoint_values();
    test_gnm_allocation_failure();

    if (g_failures == 0) {
        std::printf("All core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d check(s) failed.\n", g_failures);
    return EXIT_FAILURE;
}
