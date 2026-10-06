#include "../mod/uevr/src/utility/WuWaD3DProbePolicy.hpp"
#include <cstdlib>
#include <initializer_list>
#include <iostream>

using namespace wuwa_d3d_probe;
namespace {
int checks{};
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
}
int main() {
    auto active = next_probe(Api::None, false, false);
    check(active == Api::D3D12, "first probe preserves DX12-first behavior");
    active = next_probe(active, false, false);
    check(active == Api::D3D11, "dummy DX12 success without real Present cannot starve DX11");
    active = next_probe(active, false, false);
    check(active == Api::D3D12, "unobserved DX11 probe permits another DX12 attempt");
    for (auto api : {Api::D3D11, Api::D3D12}) {
        check(next_probe(api, false, true) == api, "real Present locks renderer before 60-frame initialization");
        check(next_probe(api, true, false) == api, "initialized renderer never alternates on timeout");
        check(next_probe(api, true, true) == api, "working renderer keeps original recovery path");
        check(next_probe(api, false, true) == api, "device-reset initialization flag does not discard observed API");
    }
    std::cout << "PASS D3D probe policy: " << checks << " checks\n";
}
