#ifndef MATMUL_HPP
#define MATMUL_HPP

#include <cstddef>

namespace matmul {

template<typename T>
void orl_ijk(const T* A, const T* B, T* C, size_t n) {
    std::fill(C, C + n * n, T{});
    for(size_t i = 0;i < n; ++i) {
        for(size_t j = 0;j < n;++j) {
            for(size_t k = 0;k < n;++k) {
                C[i*n+j] += A[i*n+k] * B[k*n+j];
            }
        }
    }
}

template<typename T>
void ijk(const T* A, const T* B, T* C, size_t n) {
    std::fill(C, C + n * n, T{});
    for(size_t i = 0;i < n; ++i) {
        for(size_t j = 0;j < n;++j) {
            T sum = T{};
            const T* Ai = A + i*n;
            for(size_t k = 0;k < n;++k) {
                sum += Ai[k] * B[k*n+j];
            }
            C[i*n+j] = sum;
        }
    }
}

template<typename T>
void ikj(const T* A, const T* B, T* C, size_t n) {
    std::fill(C, C + n * n, T{});
    for(size_t i = 0;i < n; ++i) {
        T* Ci = C + i*n;
        const T* Ai = A + i*n;
        for(size_t k = 0;k < n;++k) {
            T a_val = Ai[k];
            const T* Bk = B + k*n;
            for(size_t j = 0;j < n;++j) {
                Ci[j] += a_val * Bk[j];
            }
        }
    }
}

template<typename T>
void jik(const T* A, const T* B, T* C, size_t n) {
    std::fill(C, C + n * n, T{});
    for(size_t j = 0;j < n; ++j) {
        for(size_t i = 0;i < n;++i) {
            T sum = T{};
            const T* Ai = A + i*n;
            for(size_t k = 0;k < n;++k) {
                sum += Ai[k] * B[k*n+j];
            }
            C[i*n+j] = sum;
        }
    }
}

// block ikj
template<typename T>
void blocked(const T* A, const T* B, T* C, size_t n, size_t BS) {
    std::fill(C, C + n * n, T{});
    for(size_t ii = 0;ii < n; ii += BS) {
        size_t i_max = std::min(ii + BS, n);
        for(size_t kk = 0;kk < n; kk += BS) {
            size_t k_max = std::min(kk + BS, n);
            for(size_t jj = 0; jj < n; jj += BS) {
                size_t j_max = std::min(jj + BS, n);
                for(size_t i = ii;i < i_max; ++i) {
                    T* Ci = C + i*n;
                    const T* Ai = A + i*n;
                    for(size_t k = kk; k < k_max; ++k) {
                        T a_val = Ai[k];
                        const T* Bk = B + k*n;
                        for(size_t j = jj; j< j_max; ++j) {
                            Ci[j] += a_val * Bk[j];
                        }
                    }
                }
            }
        }
    }
}

// trans b + ikj
template<typename T>
void transposed(const T* A, const T* B, T* C, T* Bt, size_t n) {
    std::fill(C, C + n * n, T{});
    for(size_t i = 0; i < n; ++i) {
        for(size_t j = 0;j < n; ++j) {
            Bt[j*n+i] = B[i*n+j];
        }
    }

    for(size_t i = 0;i < n; ++i) {
        T* Ci = C + i*n;
        const T* Ai = A + i*n;
        for(size_t j = 0; j < n; ++j) {
            const T* Bj = Bt + j*n;
            T sum = T{};
            for(size_t k = 0; k < n;++k) {
                sum += Ai[k] * Bj[k];
            }
            Ci[j] = sum;
        }
    }
}

}   // namespace matmul
#endif // MATMUL_HPP