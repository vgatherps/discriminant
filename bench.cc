#include "discriminator.hh"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace std;

// The test message is systematically built because I don't want to deal with
// a 300+ character long string in the code

// Technically, one shouldn't repeat message headers.
// But since they aren't interesting, both of my algorithms ignore them so it's easier

// The generator that will be used here is highly overtesting, but that's
// basically the point

constexpr uint8_t BULK[] = 
    "@=a:123@=c:q@=d:much bigger message mix things up@=e:medium";

constexpr uint8_t A[] =  "=A:abcd";
constexpr uint8_t B[] =  "=B:abcd";
constexpr uint8_t C[] =  "=C:abcd";

template<int N>
constexpr int static_len(const uint8_t (&data)[N]) {
    return N-1;
}

constexpr size_t A_OFFSET = 0 * static_len(A) + 0 * static_len(BULK) + 1;
constexpr size_t B_OFFSET = 1 * static_len(A) + 6 * static_len(BULK) + 1;
constexpr size_t C_OFFSET = 2 * static_len(A) + 12 * static_len(BULK) + 1;

void append(vector<uint8_t>& vec, const uint8_t *data) {
    while (*data) {
        vec.push_back(*data);
        data++;
    }
}

vector<uint8_t> build_message() {
    vector<uint8_t> data;

    append(data, A);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, B);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, BULK);
    append(data, C);

    data.push_back(0);
    return data;
}

__attribute__ ((noinline)) int branch_test(const std::vector<uint8_t>& test) {
    if (test[A_OFFSET] == 'A') {
        return 1;
    } else if (test[B_OFFSET] == 'B') {
        return 2;
    } else if (test[C_OFFSET] == 'C') {
        return 3;
    }
    return -1;
}

void mfence() {
    __asm volatile("mfence" :::"memory");
}

template<class T>
__attribute__ ((noinline))
void black_hole(const T& a) {
    __asm volatile("" ::"r"(a): "memory");
}

uint64_t rdtscp() {
    unsigned hi, lo;
    __asm volatile ("rdtscp" : "=a"(lo), "=d"(hi) :: "memory", "rcx");
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}


template<class F, class P>
double bench_runner(const F& f, const P& p) {
    double total = 0;
    const int runs = 1000000;
    for (int i = 0; i < runs; i++) {
        // run it once to pre-cache desired data
        f();
        // run any pre-work (here it's possibly uncaching)
        p();

        mfence();
        uint64_t start = rdtscp();
        f();
        uint64_t end = rdtscp();
        mfence();
        // filter out backwards timers and blips like context switches
        if (start < end) {
            total += end - start;
        }
    }
    return total / runs;
}

template<class F, class P>
void bench(const char *name, const F& f, const P& p) {
    cout << "Ran test: " << name << ", got average cycles: " << bench_runner(f, p) << endl;
}

int main() {
    constexpr auto msg_constraints = generate_constraint_set({
        make_constraint(A_OFFSET, {'A', 'B', 'C'}, {false, true, true}),
        make_constraint(B_OFFSET, {'B', 'C', 'A'}, {true, false, true}),
        make_constraint(C_OFFSET, {'C', 'A', 'B'}, {true, true, false}),
    });


    auto val = build_message();

    assert(val[A_OFFSET] == 'A');
    assert(val[B_OFFSET] == 'B');
    assert(val[C_OFFSET] == 'C');

    auto dummy = [](){};
    auto clear_val = [&]() {
        for (int i = 0; i < val.size(); i += 64) {
            _mm_clflush(&val[i]);
        }
        mfence();
    };

    auto fast_check = [&]() {
        black_hole(check_byte_buffer(val.data(), val.size(), msg_constraints));
    };
    auto fast_check_nolen = [&]() {
        black_hole(check_byte_buffer(val.data(), msg_constraints));
    };
    auto brancher = [&](){
        black_hole(branch_test(val));
    };

    cout << "Message contains " << val.size() - 1 << " bytes" << endl;

    cout << "Time spent taking measurements: " << bench_runner(dummy, dummy) << endl;
    
    bench("fast with length", fast_check, dummy);
    bench("fast without length checks", fast_check_nolen, dummy);
    bench("optimal branching", brancher, dummy);

    bench("fast with length nocache", fast_check, clear_val);
    bench("fast without length checks nocache", fast_check_nolen, clear_val);
    bench("optimal branching nocache", brancher, clear_val);
}
