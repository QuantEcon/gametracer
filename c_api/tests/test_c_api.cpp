// Tests for the GameTracer C ABI (c_api/gametracer_c_api.h).
//
// Build and run from the repository root:
//   cmake -S c_api -B build -DGAMETRACER_BUILD_TESTS=ON
//   cmake --build build
//   ctest --test-dir build --output-on-failure

#include "gametracer_c_api.h"

#include <climits>
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

// A normal form game in the layout expected by the C ABI.
struct Game {
    int N;
    std::vector<int> actions;
    std::vector<double> payoffs;  // length N * P, player-major blocks

    int M() const {
        int m = 0;
        for (int p = 0; p < N; ++p) m += actions[p];
        return m;
    }
    int P() const {
        int prod = 1;
        for (int p = 0; p < N; ++p) prod *= actions[p];
        return prod;
    }
    int offset(int p) const {
        int o = 0;
        for (int k = 0; k < p; ++k) o += actions[k];
        return o;
    }
    // profile_index(s) = s0 + s1*a0 + s2*a0*a1 + ...
    int profile_index(const std::vector<int>& s) const {
        int idx = 0, stride = 1;
        for (int p = 0; p < N; ++p) {
            idx += s[p] * stride;
            stride *= actions[p];
        }
        return idx;
    }
    double payoff(int player, const std::vector<int>& s) const {
        return payoffs[static_cast<size_t>(player) * P() + profile_index(s)];
    }
};

// Expected payoff to `player` from pure action `a` when the other players
// play the mixed actions in the flattened profile `x`.
double pure_action_payoff(const Game& g, int player, int a, const double* x) {
    std::vector<int> s(g.N, 0);
    double total = 0.0;
    const int P = g.P();
    for (int idx = 0; idx < P; ++idx) {
        // decode idx into s (first player varying fastest)
        int rem = idx;
        for (int p = 0; p < g.N; ++p) {
            s[p] = rem % g.actions[p];
            rem /= g.actions[p];
        }
        if (s[player] != a) continue;
        double w = 1.0;
        for (int p = 0; p < g.N; ++p) {
            if (p == player) continue;
            w *= x[g.offset(p) + s[p]];
        }
        total += w * g.payoffs[static_cast<size_t>(player) * P + idx];
    }
    return total;
}

bool is_mixed_action_profile(const Game& g, const double* x, double tol) {
    for (int p = 0; p < g.N; ++p) {
        double sum = 0.0;
        for (int a = 0; a < g.actions[p]; ++a) {
            double v = x[g.offset(p) + a];
            if (!(v >= -tol)) return false;
            sum += v;
        }
        if (std::fabs(sum - 1.0) > tol) return false;
    }
    return true;
}

// Nash equilibrium check: every action in the support is a best response
// (up to `tol`), and the profile lies in the product of simplices.
bool is_nash(const Game& g, const double* x, double tol) {
    if (!is_mixed_action_profile(g, x, tol)) return false;
    for (int p = 0; p < g.N; ++p) {
        double best = -HUGE_VAL;
        std::vector<double> u(g.actions[p]);
        for (int a = 0; a < g.actions[p]; ++a) {
            u[a] = pure_action_payoff(g, p, a, x);
            if (u[a] > best) best = u[a];
        }
        for (int a = 0; a < g.actions[p]; ++a) {
            if (x[g.offset(p) + a] > tol && u[a] < best - tol) return false;
        }
    }
    return true;
}

bool close_profile(const double* x, const std::vector<double>& y, double tol) {
    for (size_t i = 0; i < y.size(); ++i)
        if (std::fabs(x[i] - y[i]) > tol) return false;
    return true;
}

// 3x2 game from von Stengel (2007), as in c_api/README.md, with equilibria
//   ([1, 0, 0], [1, 0]), ([4/5, 1/5, 0], [2/3, 1/3]), ([0, 1/3, 2/3], [1/3, 2/3])
Game von_stengel_3x2() {
    Game g;
    g.N = 2;
    g.actions = {3, 2};
    g.payoffs = {
        3.0, 2.0, 0.0, 3.0, 5.0, 6.0,  // player 1
        3.0, 2.0, 3.0, 2.0, 6.0, 1.0   // player 2
    };
    return g;
}

// 2x2x2 game from McKelvey and McLennan (1996) with 9 Nash equilibria.
Game mckelvey_mclennan_2x2x2() {
    Game g;
    g.N = 3;
    g.actions = {2, 2, 2};
    g.payoffs.assign(3 * 8, 0.0);
    struct Entry { int s[3]; double u[3]; };
    const Entry entries[] = {
        {{0, 0, 0}, {9, 8, 12}},
        {{1, 1, 0}, {9, 8, 2}},
        {{0, 1, 1}, {3, 4, 6}},
        {{1, 0, 1}, {3, 4, 4}},
    };
    for (const Entry& e : entries) {
        std::vector<int> s(e.s, e.s + 3);
        for (int p = 0; p < 3; ++p)
            g.payoffs[static_cast<size_t>(p) * 8 + g.profile_index(s)] = e.u[p];
    }
    return g;
}

int call_ipa(const Game& g, const std::vector<double>& ray,
             std::vector<double>& zh, double alpha, double fuzz,
             std::vector<double>& out) {
    out.assign(g.M(), 0.0);
    return ipa(g.N, g.actions.data(), g.payoffs.data(), ray.data(), zh.data(),
               alpha, fuzz, out.data());
}

// Returns the number of equilibria (or a negative error code) and fills
// `eqs` with the equilibria, each of length M.
int call_gnm(const Game& g, const std::vector<double>& ray,
             std::vector<std::vector<double> >& eqs,
             int steps = 100, double fuzz = 1e-12, int lnmfreq = 3,
             int lnmmax = 10, double lambdamin = -10.0, int wobble = 0,
             double threshold = 1e-2) {
    eqs.clear();
    double* answers = reinterpret_cast<double*>(0x1);  // must be reset by gnm
    int ret = gnm(g.N, g.actions.data(), g.payoffs.data(), ray.data(), &answers,
                  steps, fuzz, lnmfreq, lnmmax, lambdamin, wobble, threshold);
    const int M = g.M();
    if (ret > 0) {
        CHECK(answers != NULL);
        for (int k = 0; k < ret; ++k)
            eqs.push_back(std::vector<double>(answers + k * M, answers + (k + 1) * M));
    } else {
        CHECK(answers == NULL);
    }
    gametracer_free(answers);
    return ret;
}

void test_ipa_von_stengel() {
    Game g = von_stengel_3x2();
    std::vector<double> ray = {0.0, 0.0, 1.0, 0.0, 1.0};
    std::vector<double> zh = {1.0 / 3, 1.0 / 3, 1.0 / 3, 0.5, 0.5};
    std::vector<double> out;
    int ret = call_ipa(g, ray, zh, 0.02, 1e-6, out);
    CHECK(ret == 1);
    CHECK(is_nash(g, out.data(), 1e-6));
    CHECK(close_profile(out.data(), {0.0, 1.0 / 3, 2.0 / 3, 1.0 / 3, 2.0 / 3}, 1e-6));
}

void test_gnm_von_stengel() {
    Game g = von_stengel_3x2();
    std::vector<double> ray = {0.0, 0.0, 1.0, 0.0, 1.0};
    std::vector<std::vector<double> > eqs;
    int ret = call_gnm(g, ray, eqs);
    CHECK(ret == 3);
    for (size_t k = 0; k < eqs.size(); ++k)
        CHECK(is_nash(g, eqs[k].data(), 1e-8));
    // The ray is not modified by the shim
    CHECK(close_profile(ray.data(), {0.0, 0.0, 1.0, 0.0, 1.0}, 0.0));
}

void test_ipa_mckelvey_mclennan() {
    Game g = mckelvey_mclennan_2x2x2();
    std::vector<double> ray = {0.3, 0.7, 0.6, 0.4, 0.2, 0.8};
    std::vector<double> zh(g.M(), 1.0);
    std::vector<double> out;
    int ret = call_ipa(g, ray, zh, 0.02, 1e-6, out);
    CHECK(ret == 1);
    CHECK(is_nash(g, out.data(), 1e-5));
}

void test_gnm_mckelvey_mclennan() {
    Game g = mckelvey_mclennan_2x2x2();
    std::vector<double> ray = {0.3, 0.7, 0.6, 0.4, 0.2, 0.8};
    std::vector<std::vector<double> > eqs;
    int ret = call_gnm(g, ray, eqs);
    CHECK(ret > 0);
    CHECK(static_cast<int>(eqs.size()) == ret);
    for (size_t k = 0; k < eqs.size(); ++k)
        CHECK(is_nash(g, eqs[k].data(), 1e-8));
}

void test_invalid_arguments() {
    Game g = von_stengel_3x2();
    std::vector<double> ray(g.M(), 1.0), zh(g.M(), 1.0), out(g.M());
    double* answers = NULL;

    // null pointers
    CHECK(ipa(g.N, NULL, g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
    CHECK(ipa(g.N, g.actions.data(), NULL, ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
    CHECK(ipa(g.N, g.actions.data(), g.payoffs.data(), NULL, zh.data(), 0.02, 1e-6, out.data()) == -1);
    CHECK(ipa(g.N, g.actions.data(), g.payoffs.data(), ray.data(), NULL, 0.02, 1e-6, out.data()) == -1);
    CHECK(ipa(g.N, g.actions.data(), g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, NULL) == -1);
    CHECK(gnm(g.N, NULL, g.payoffs.data(), ray.data(), &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
    CHECK(answers == NULL);
    CHECK(gnm(g.N, g.actions.data(), g.payoffs.data(), ray.data(), NULL, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);

    // fewer than two players
    {
        int actions1[] = {3};
        double payoffs1[] = {1.0, 2.0, 3.0};
        double ray1[] = {0.1, 0.2, 0.3}, zh1[] = {1.0, 1.0, 1.0}, out1[3];
        CHECK(ipa(1, actions1, payoffs1, ray1, zh1, 0.02, 1e-6, out1) == -1);
        CHECK(gnm(1, actions1, payoffs1, ray1, &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
        CHECK(answers == NULL);
        CHECK(ipa(0, actions1, payoffs1, ray1, zh1, 0.02, 1e-6, out1) == -1);
    }

    // non-positive action counts
    {
        int bad_actions[] = {3, 0};
        CHECK(ipa(2, bad_actions, g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
        CHECK(gnm(2, bad_actions, g.payoffs.data(), ray.data(), &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
    }

    // size overflow
    {
        int huge_actions[] = {INT_MAX, INT_MAX};
        CHECK(ipa(2, huge_actions, g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
        CHECK(gnm(2, huge_actions, g.payoffs.data(), ray.data(), &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
    }

    // M = 65536 passes the M, P, N*P checks but M*M overflows int in the core
    {
        int big_actions[] = {65535, 1};
        CHECK(ipa(2, big_actions, g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
        CHECK(gnm(2, big_actions, g.payoffs.data(), ray.data(), &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
        CHECK(answers == NULL);
    }

    // N*P = 32 * 2^27 = 2^32 exceeds INT_MAX; on a 32-bit size_t the
    // product wraps to 0, so it must be checked before multiplying
    {
        int many_actions[32];
        for (int p = 0; p < 32; ++p) many_actions[p] = (p < 27) ? 2 : 1;
        CHECK(ipa(32, many_actions, g.payoffs.data(), ray.data(), zh.data(), 0.02, 1e-6, out.data()) == -1);
        CHECK(gnm(32, many_actions, g.payoffs.data(), ray.data(), &answers, 100, 1e-12, 3, 10, -10.0, 0, 1e-2) == -1);
        CHECK(answers == NULL);
    }

    // gametracer_free is safe on NULL
    gametracer_free(NULL);
}

}  // namespace

int main() {
    test_ipa_von_stengel();
    test_gnm_von_stengel();
    test_ipa_mckelvey_mclennan();
    test_gnm_mckelvey_mclennan();
    test_invalid_arguments();

    if (g_failures == 0) {
        std::printf("All C API tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d check(s) failed.\n", g_failures);
    return EXIT_FAILURE;
}
