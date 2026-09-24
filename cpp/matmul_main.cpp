#include <cstdio>
#include <cmath>
#include <vector>
#include "hpp/hvector.hpp"
#include "hpp/matmul.hpp"

using T = double;

// 参考实现：纯 CPU 三重循环，最朴素但保证正确
void ref_ijk(const T* A, const T* B, T* C, size_t n) {
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            T s = 0;
            for (size_t k = 0; k < n; ++k) s += A[i*n+k] * B[k*n+j];
            C[i*n+j] = s;
        }
}

bool check(const std::vector<T>& ref, const std::vector<T>& got, size_t n) {
    for (size_t i = 0; i < n*n; ++i)
        if (std::fabs(ref[i] - got[i]) > 1e-9) return false;
    return true;
}

int main() {
    const size_t n = 32;
    std::vector<T> A(n*n), B(n*n), C_ref(n*n), C(n*n), Bt(n*n);
    for (size_t i = 0; i < n*n; ++i) { A[i] = (i % 7) + 1; B[i] = (i % 11) + 1; }

    ref_ijk(A.data(), B.data(), C_ref.data(), n);

    matmul::ijk(A.data(), B.data(), C.data(), n);
    printf("ijk        : %s\n", check(C_ref, C, n) ? "OK" : "FAIL");

    matmul::ikj(A.data(), B.data(), C.data(), n);
    printf("ikj        : %s\n", check(C_ref, C, n) ? "OK" : "FAIL");

    matmul::jik(A.data(), B.data(), C.data(), n);
    printf("jik        : %s\n", check(C_ref, C, n) ? "OK" : "FAIL");

    matmul::blocked(A.data(), B.data(), C.data(), n, 16);
    printf("blocked    : %s\n", check(C_ref, C, n) ? "OK" : "FAIL");

    matmul::transposed(A.data(), B.data(), C.data(), Bt.data(), n);
    printf("transposed : %s\n", check(C_ref, C, n) ? "OK" : "FAIL");

    return 0;
}