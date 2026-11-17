#include "Test.h"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const auto& test : tourelle::test::Registry()) {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) continue;
        const int before = tourelle::test::Failures();
        test.body();
        std::printf("%s %s\n", tourelle::test::Failures() == before ? "[ ok ]" : "[FAIL]", test.name);
        ++ran;
    }
    std::printf("%d test(s), %d failure(s)\n", ran, tourelle::test::Failures());
    return tourelle::test::Failures() == 0 && ran > 0 ? 0 : 1;
}
