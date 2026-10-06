#define NOMINMAX
#include "../uevr/src/utility/WuWaPlayStationHid.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace wuwa_ps;
static int passed{};
static void check(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
    ++passed;
}
template<size_t N> static void crc(std::array<uint8_t, N>& r) {
    uint32_t value = crc_byte(0xffffffffu, 0xa1);
    for (int i = 0; i < 74; ++i) value = crc_byte(value, r[i]);
    value ^= 0xffffffffu;
    for (int i = 0; i < 4; ++i) r[74 + i] = uint8_t(value >> (8 * i));
}
static void report_case(Model kind, size_t size, uint8_t id, size_t base, size_t buttons, size_t triggers) {
    std::array<uint8_t,547> r{}; r[0] = id;
    if (id == 0x11) r[1] = 0x80;
    r[base] = 0; r[base + 1] = 255; r[base + 2] = 127; r[base + 3] = 128;
    r[buttons] = 0xf0 | 3; r[buttons + 1] = 0xf3;
    r[triggers] = 180; r[triggers + 1] = 29;
    if (id == 0x11 || id == 0x31) crc(r);
    Pad p{};
    check(decode(kind, r.data(), size, p), "valid report rejected");
    check(p.buttons == (0xf000 | 0x03f0 | 2 | 8), "buttons did not normalize to shared vocabulary");
    check(p.lt == 180 && p.rt == 29, "analogue triggers combined or quantized");
    check(p.lx == -32768 && p.ly == -32767 && p.rx == 0 && p.ry == 0, "axis orientation/center wrong");
    for (int hat = 0; hat < 9; ++hat) {
        r[buttons] = uint8_t(hat); if (id != 1) crc(r);
        check(decode(kind, r.data(), size, p), "valid D-pad direction rejected");
    }
    const auto saved = p;
    r[buttons] = 15; if (id != 1) crc(r);
    check(!decode(kind, r.data(), size, p) && p.buttons == saved.buttons, "malformed report modified last good state");
    if (id != 1) {
        r[buttons] = 8; crc(r); r[triggers] ^= 1;
        check(!decode(kind, r.data(), size, p), "corrupt Bluetooth CRC accepted");
    }
}
int main(int argc, char** argv) try {
    if (argc == 2) {
        const std::string mode=argv[1];
        if (mode!="--reader-stop" && mode!="--reader-process-exit") return 2;
        auto reader=std::make_unique<wuwa_ps_hid::Reader>();
        reader->start();
        Sleep(350); // Own-process lifecycle check; shared read-only HID access.
        const auto sample=reader->snapshot();
        check(sample.started!=0,"real module pin/start failed");
        std::cout << "Reader started: " << reader->status() << '\n' << std::flush;
        if (mode=="--reader-process-exit") {
            wuwa_ps_hid::release_on_process_exit(reader,true);
            check(!reader,"exit branch retained owner");
            std::cout << "PASS process-exit owner release; Windows reclaims worker\n" << std::flush;
            ExitProcess(0);
        }
        reader.reset();
        std::cout << "PASS normal reader stop/join; no device writes\n";
        return 0;
    }
    check(model(0x054c,0x05c4) == Model::ds4 && model(0x054c,0x09cc) == Model::ds4, "DS4 identity");
    check(model(0x054c,0x0ce6) == Model::dualsense && model(0x054c,0x0df2) == Model::dualsense, "DualSense identity");
    check(model(0x1234,0x0ce6) == Model::unsupported && model(0x054c,1) == Model::unsupported, "unknown device accepted");
    report_case(Model::ds4,64,1,1,5,8);
    report_case(Model::ds4,10,1,1,5,8);
    report_case(Model::ds4,128,1,1,5,8);
    report_case(Model::ds4,78,0x11,3,7,10);
    report_case(Model::ds4,547,1,1,5,8);
    report_case(Model::ds4,547,0x11,3,7,10);
    report_case(Model::dualsense,64,1,1,8,5);
    report_case(Model::dualsense,10,1,1,5,8);
    report_case(Model::dualsense,78,1,1,5,8);
    report_case(Model::dualsense,78,0x31,2,9,6);
    Pad p{}; std::array<uint8_t,128> r{}; r[0]=1;
    for (size_t n=0;n<10;++n) check(!decode(Model::ds4,r.data(),n,p),"truncated report accepted");
    check(!decode(Model::dualsense,r.data(),63,p),"ambiguous report length accepted");
    check(!decode(Model::unsupported,r.data(),64,p) && !decode(Model::ds4,nullptr,64,p),"unsupported/null accepted");
    check(!fallback_allowed(1000,0,1500,1500),"cold route was not delayed");
    check(fallback_allowed(1000,0,4000,4000),"fresh native fallback unavailable");
    check(!fallback_allowed(1000,2000,4000,4000),"mapped XInput duplicate not suppressed");
    check(fallback_allowed(1000,1999,4000,4000),"XInput timeout boundary");
    check(!fallback_allowed(1000,0,3000,4000) && !fallback_allowed(5000,0,4000,4000),"stale/reversed clock accepted");
    MenuGesture menu; Pad both{}; both.buttons=0xc0;
    check(!menu.update(both,1000,true,false),"held initial menu fired");
    check(!menu.update({},1010,true,false) && menu.update(both,1020,true,false),"neutral menu arming failed");
    check(!menu.update(both,4000,true,false),"held menu repeated");
    check(!menu.update({},4010,true,false) && !menu.update(both,4020,true,true) &&
        !menu.update(both,5019,true,true) && menu.update(both,5020,true,true),"menu long hold boundary");
    menu.update(both,5030,false,false);
    check(!menu.update(both,5040,true,false),"focus/source reset failed to disarm menu");
    wuwa_ps_hid::last_xinput=0; wuwa_ps_hid::hid_menu_held=true; wuwa_ps_hid::xinput_menu_rearm=false;
    XINPUT_GAMEPAD xp{}; xp.wButtons=0xc0;
    wuwa_ps_hid::observe_xinput(xp,10000);
    check(wuwa_ps_hid::suppress_xinput_menu(),"Steam Input takeover repeated native menu chord");
    wuwa_ps_hid::observe_xinput(xp,13000);
    check(wuwa_ps_hid::suppress_xinput_menu(),"held mapped menu rearmed");
    wuwa_ps_hid::observe_xinput({},13010);
    check(wuwa_ps_hid::suppress_xinput_menu(),"other neutral Xbox pad cleared physical held-menu fence");
    wuwa_ps_hid::hid_menu_held=false;
    wuwa_ps_hid::observe_xinput({},13020);
    check(!wuwa_ps_hid::suppress_xinput_menu(),"neutral mapped input did not rearm");
    // Construct/destruct the actual Win32 reader without starting it. Tests
    // never enumerate, open or read any real controller.
    wuwa_ps_hid::Reader reader;
    check(reader.snapshot().stamp==0,"unstarted reader claimed input");
    wuwa_ps_hid::Reader denied{+[] { return false; }};
    denied.start(); denied.start();
    check(denied.snapshot().stamp==0 && denied.status().find("cannot retain backend")!=std::string::npos,
        "pin failure started device discovery or hid failure status");
    struct Owned { int& destroyed; ~Owned() { ++destroyed; } };
    int destroyed{};
    auto owned=std::make_unique<Owned>(Owned{destroyed}); destroyed=0;
    auto* raw=owned.get();
    wuwa_ps_hid::release_on_process_exit(owned,false);
    check(owned.get()==raw && destroyed==0,"ordinary owner was abandoned");
    wuwa_ps_hid::release_on_process_exit(owned,true);
    check(!owned && destroyed==0,"process exit destroyed potentially abandoned worker state");
    delete raw; // Test-only reclamation after proving production release policy.
    std::cout << "PASS " << passed << " PlayStation decoder/source/menu checks (no device access)\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
