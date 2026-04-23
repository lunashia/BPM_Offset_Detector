#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mathalgo {

/*
 * WHAT:
 *   Solve least-squares polynomial regression:
 *     y ~= c0 + c1*x + ... + c_degree*x^degree
 *   Returns coefficients [c0, c1, ...].
 *
 * WHY:
 *   Used by offset refinement to estimate sub-bin peak location with a compact
 *   quadratic fit, which reduces discretization error versus integer-bin peaks.
 *
 * ASSUMPTIONS:
 *   - count > degree and x values provide enough numeric variation.
 *   - Near-singular systems return zero coefficients as a safe fallback.
 */
template <typename T>
std::vector<T> polyfit(const T* x, const T* y, size_t count, int degree)
{
    if(count == 0u || degree < 0) return {};

    const int cols = degree + 1;
    const int rows = static_cast<int>(count);

    // Normal equations: (X^T X) c = X^T y
    std::vector<T> a(static_cast<size_t>(cols * cols), T(0));
    std::vector<T> b(static_cast<size_t>(cols), T(0));

    for(int r = 0; r < rows; ++r) {
        std::vector<T> xp(static_cast<size_t>(cols), T(1));
        for(int c = 1; c < cols; ++c) {
            xp[static_cast<size_t>(c)] = xp[static_cast<size_t>(c - 1)] * x[r];
        }

        for(int i = 0; i < cols; ++i) {
            b[static_cast<size_t>(i)] += xp[static_cast<size_t>(i)] * y[r];
            for(int j = 0; j < cols; ++j) {
                a[static_cast<size_t>(i * cols + j)] += xp[static_cast<size_t>(i)] * xp[static_cast<size_t>(j)];
            }
        }
    }

    // Gaussian elimination with partial pivoting.
    for(int i = 0; i < cols; ++i) {
        int pivot = i;
        T maxAbs = std::fabs(a[static_cast<size_t>(i * cols + i)]);
        for(int r = i + 1; r < cols; ++r) {
            T v = std::fabs(a[static_cast<size_t>(r * cols + i)]);
            if(v > maxAbs) {
                maxAbs = v;
                pivot = r;
            }
        }

        if(maxAbs <= T(1e-12)) {
            return std::vector<T>(static_cast<size_t>(cols), T(0));
        }

        if(pivot != i) {
            for(int c = i; c < cols; ++c) {
                std::swap(a[static_cast<size_t>(i * cols + c)], a[static_cast<size_t>(pivot * cols + c)]);
            }
            std::swap(b[static_cast<size_t>(i)], b[static_cast<size_t>(pivot)]);
        }

        T diag = a[static_cast<size_t>(i * cols + i)];
        for(int c = i; c < cols; ++c) {
            a[static_cast<size_t>(i * cols + c)] /= diag;
        }
        b[static_cast<size_t>(i)] /= diag;

        for(int r = 0; r < cols; ++r) {
            if(r == i) continue;
            T factor = a[static_cast<size_t>(r * cols + i)];
            if(std::fabs(factor) <= T(1e-20)) continue;

            for(int c = i; c < cols; ++c) {
                a[static_cast<size_t>(r * cols + c)] -= factor * a[static_cast<size_t>(i * cols + c)];
            }
            b[static_cast<size_t>(r)] -= factor * b[static_cast<size_t>(i)];
        }
    }

    return b;
}

} // namespace mathalgo
