#include "api.hpp"
#include "widgets.hpp"
#include <iostream>
int main() {
    int count = 0;
    auto check = [&](bool v) {
        if (!v)
            throw std::runtime_error("Native adapter assertion failed");
        ++count;
    };
    check(std::abs(plotNumber("123.456") - 123.456) < 1e-10);
    check(plotNumber(0) == 0);
    check(std::isnan(plotNumber(nullptr)));
    check(std::isnan(plotNumber("12bad")));
    check(std::isnan(plotNumber("nan")));
    check(std::isnan(plotNumber("inf")));
    check(LocalApi::encode("Apple & AAPL") == "Apple%20%26%20AAPL");
    check(LocalApi::encode("../orders?x=1") == "..%2Forders%3Fx%3D1");
    std::atomic<bool> stop = false;
    bool rejected = false;
    try {
        LocalApi invalid(0, "user", stop);
    } catch (...) {
        rejected = true;
    }
    check(rejected);
    std::cout << count << " native data-adapter assertions passed\n";
}
