// Regression tests that call the C++ core directly (the core symbols are not
// exported from the shared library, so this program compiles the sources in).
//
// Build and run from the repository root:
//   cmake -S c_api -B build -DGAMETRACER_BUILD_TESTS=ON
//   cmake --build build
//   ctest --test-dir build --output-on-failure

#include "cmatrix.h"
#include "nfgame.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

}  // namespace

int main() {
    test_payoff_matrix_large_game();
    test_adjoint_large_matrix();
    test_adjoint_values();

    if (g_failures == 0) {
        std::printf("All core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d check(s) failed.\n", g_failures);
    return EXIT_FAILURE;
}
