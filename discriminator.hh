#include <iostream>
#include <bitset>
using namespace std;
#include <array>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

struct eq8_constraint {
   alignas(16) std::uint8_t bytes[16] = {0};
   alignas(16) std::uint8_t ignores[16] = {0};
   std::size_t offset = 0;
};

template<std::size_t N>
struct constraint_spec {
    std::array<std::uint8_t, N> bytes = {0};
    std::array<std::uint8_t, N> ignores = {0};
    std::size_t offset = 0;
};

template<std::size_t J, std::size_t N>
struct constraint_set {
    std::array<eq8_constraint, J> constraints;
    std::array<int, J> length_mask;
};

template<std::size_t N, class I_TYPE>
constexpr inline constraint_spec<N> make_constraint(
    std::size_t offset,
    const I_TYPE(&bytes)[N])
{
    constraint_spec<N> spec;
    spec.offset = offset;
    for (int i = 0; i < N; i++) {
        spec.bytes[i] = bytes[i];
    }
    return spec;
}

template<size_t N, class I_TYPE>
constexpr inline constraint_spec<N> make_constraint(
    std::size_t offset,
    const I_TYPE(&bytes)[N],
    const bool(&cares)[N])
{
    constraint_spec<N> spec;
    spec.offset = offset;
    // check that the ignores flags make sense
    bool all_care = true;
    bool all_ignore = true;
    for (bool care: cares) {
        all_care &= care;
        all_ignore &= !care;
    }

    if (all_care) {
        throw "All elements are manually specified to care";
    }
    if (all_ignore) {
        throw "All elements are manually specified to ignore";
    }

    for (int i = 0; i < N; i++) {
        spec.bytes[i] = bytes[i];
        spec.ignores[i] = cares[i] ? 0 : -1;
    }
    return spec;
}

template<size_t N, typename std::enable_if<N == 16, int>::type = 0>
inline __m128i read_from(const std::uint8_t(&arr)[N])
{
    __m128i check = _mm_set1_epi8(0);
    static_assert(sizeof(check) == sizeof(arr), "Check that byte buffer is same as sse vector");
    std::memcpy(&check, arr, sizeof(check));
    return check;
}

template<std::size_t J, std::size_t N>
constexpr inline constraint_set<J, N> generate_constraint_set(
    const constraint_spec<N>(&specs)[J])
{
    static_assert(J > 0, "Constraint set with no constraints makes no sense");
    static_assert(N <= 16, "Can't have more than 16 values in constraint");
    static_assert(N > 0, "Must have more than zero values in constraint");

    // Check that each offset is unique
    for (int j = 0; j < J; j++) {
        for (int jj = 0; jj < J; jj++) {
            if (jj == j) {
                continue;
            }
            if (specs[j].offset == specs[jj].offset) {
                throw "Duplicate indices found in constraint list";
            }
        }
    }

    // Check that each constraint stream is unique
    for (int i = 0; i < N; i++) {
        for (int ii = 0; ii < N; ii++) {
            if (ii == i) {
                continue;
            }

            bool was_same = true;
            for (int j = 0; j < J; j++) {
                bool same_value = specs[j].bytes[i] == specs[j].bytes[ii];
                bool one_ignored = specs[j].ignores[i] || specs[j].ignores[ii];
                was_same &= same_value || one_ignored;
            }
            if (was_same) {
                throw "Duplicate stream of constraints found in specification";
            }
        } 
    }

    // check that each index in the array is sorted
    for (int j = 0; j < J-1; j++) {
        if (specs[j].offset > specs[j+1].offset) {
            throw "Input offsets are not sorted";
        }
    }

    std::array<eq8_constraint, J> constraints;

    for (int j = 0; j < J; j++) {
        constraints[j].offset = specs[j].offset;
        for (int i = 0; i < N; i++) {
            constraints[j].bytes[i] = specs[j].bytes[i];
            constraints[j].ignores[i] = specs[j].ignores[i];
        }
    }

    std::array<int, J> length_mask = {0};

    for (int i = 0; i < N; i++) {
        length_mask[J-1] |= (specs[J-1].ignores[i] << i);
        for (int j = J-2; j >= 0; j--) {
            length_mask[j] |=
            (specs[j].ignores[i] << i) | (specs[j+1].ignores[i] << i);
        }
    }

    constraint_set<J, N> set = {
        .constraints = constraints,
        .length_mask = length_mask
    };

    return set;
}

template<std::size_t J, std::size_t N, bool check_length>
inline int check_byte_buffer_inner(
    const uint8_t *buffer,
    std::size_t length,
    const constraint_set<J, N>& checks)
{
    __m128i still_valid = _mm_set1_epi8(-1);

    if (length <= 0) {
        return 0;
    }

    int cur_length_mask = 0;
    for (int i = 0; i < J; i++) {

        // The compiler is very inconsistent at removing branches and often inefficient
        // Since I care a lot about consistent runtimes, I don't want the branch
        // predictor learning that a very important messages comes infrequently
        //
        // This will remove a few optimization opportunities around avoid mask manipulation,
        // when any failure always gives the same result, but there's enough IPC to
        // get away with it
        int mask = 0;
        std::size_t zero = 0;
        std::size_t offset = checks.constraints[i].offset;

        if (check_length) {
            __asm(
                "cmp %[offset], %[length]\n\t"
                "cmovle %[zero], %[out_index]\n\t"
                "cmovle %[adjust_mask], %[out_mask]\n\t"
                : [out_mask] "+r" (mask),
                  [out_index] "+r" (offset)
                : [offset] "ir" (offset),
                  [zero] "r" (zero),
                  [length] "r" (length),
                  [adjust_mask] "r" (checks.length_mask[i])
             );
        }

        __m128i broadcast = _mm_set1_epi8(buffer[offset]);
        __m128i ref = read_from(checks.constraints[i].bytes);
        __m128i ignores = read_from(checks.constraints[i].ignores);
        __m128i equals = _mm_cmpeq_epi8(broadcast, ref);

        still_valid &= (equals | ignores);
    }

    int end_mask = (1 << N) - 1;

    return end_mask & _mm_movemask_epi8(still_valid) & ~cur_length_mask;
}

template<std::size_t J, std::size_t N>
inline int check_byte_buffer(
    const uint8_t *buffer,
    const constraint_set<J, N>& checks)
{
    return check_byte_buffer_inner<J, N, false>(buffer, 1, checks);
}


template<std::size_t J, std::size_t N>
inline int check_byte_buffer(
    const uint8_t *buffer,
    std::size_t length,
    const constraint_set<J, N>& checks)
{
    return check_byte_buffer_inner<J, N, true>(buffer, length, checks);
}
