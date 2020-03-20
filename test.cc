#include "discriminator.hh"

#include <iostream>

using namespace std;

constexpr std::uint8_t msg_1[] = "@=a:abcd@=b:text@=c:12";
constexpr std::uint8_t msg_2[] = "@=a:afce@=b:text2@=c:12";
constexpr std::uint8_t msg_3[] = "@=a:acf@=b:text2@=c#9";
constexpr std::uint8_t msg_4[] = "@=q:ccd@-q:text23@=c$123";
constexpr std::uint8_t msg_5[] = "@=a:ccd@=q:text23@=d$123";

static constexpr auto msg_constraints = generate_constraint_set({
    make_constraint(2,  {'a', 'a', 'a', 'q'}),
    make_constraint(8,  {'@', '@', '=', '='}, {true, true, true, false}),
    make_constraint(17, {'=', '@', '=', '@'}),
    make_constraint(19, {':', 'c', '#', 'c'}),
});

__attribute__((noinline)) int check_codegen(const std::uint8_t *msg, std::size_t len) {
    return check_byte_buffer(msg, len, msg_constraints);
}

int main() {
    cout << msg_1 << " decodes to: " << check_codegen(msg_1, sizeof(msg_1)) << endl;
    cout << msg_2 << " decodes to: " << check_codegen(msg_2, sizeof(msg_2)) << endl;
    cout << msg_3 << " decodes to: " << check_codegen(msg_3, sizeof(msg_3)) << endl;
    cout << msg_4 << " decodes to: " << check_codegen(msg_4, sizeof(msg_4)) << endl;
    cout << msg_5 << " decodes to: " << check_codegen(msg_5, sizeof(msg_5)) << endl;
}
