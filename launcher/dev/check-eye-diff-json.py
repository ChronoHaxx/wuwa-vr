#!/usr/bin/env python3
"""Compile the probe's eye-diff JSON serializer and pair() call site against real nlohmann/json.

WuWaLodProbe.hpp needs Windows headers, so this extracts the two serializer functions and the
sampling call site VERBATIM from the header, wraps them in stub types with the same field
types, and runs them over fake memory. The JSONL it writes is the schema the offline
summarizer reads. Default mode fails if that output differs from the committed fixture, so
the serializer, the call site and fixtures cannot drift apart silently.

    check-eye-diff-json.py --json-include path/to/dir-containing-nlohmann   # verify
    check-eye-diff-json.py --json-include ... --update                      # rewrite fixture

Needs g++ (or $CXX) and nlohmann/json (single header, e.g. v3.11.3). Compilation here is not
an MSVC build: the full native target still needs the Windows toolchain.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
utility = (here / '../../mod/uevr/src/utility').resolve()
if not utility.is_dir():
    utility = (here / '../upstream/UEVR/src/utility').resolve()
fixture = here / 'fixtures' / 'eye-pair-diff-native-sample.jsonl'

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--json-include', required=True, help='directory that contains nlohmann/json.hpp')
parser.add_argument('--update', action='store_true', help='rewrite the committed fixture')
args = parser.parse_args()

probe = (utility / 'WuWaLodProbe.hpp').read_bytes().decode().replace('\r\n', '\n')
start = probe.index('inline Json eye_diff_region_json')
end = probe.index('struct Record {', start)
functions = probe[start:end]
site_start = probe.index('if (p->eye_diff_sampler.take(')
site_end = probe.index('} else ++p->reads_failed;', site_start)
site = probe[site_start:site_end]

harness = r'''
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <vector>
#include <cassert>
#include <iostream>
#include "WuWaEyeDiff.hpp"
using Json = nlohmann::json;
template<class T> struct Field { T value{}; bool valid{}; };
struct View { uintptr_t address{}; Field<uintptr_t> state{}; Field<uint32_t> frame{}; };
struct Rec { uint32_t thread{}, phase{}; };
struct Probe {
    wuwa_eye_diff::Ring<> eye_diff_ring;
    wuwa_eye_diff::Sampler eye_diff_sampler;
    wuwa_eye_diff::Sample eye_diff_scratch;
    std::atomic<uint64_t> eye_diff_dropped{};
};
''' + functions + r'''
constexpr uintptr_t base_a = 0x100000, base_b = 0x900000, sbase_a = 0x1100000, sbase_b = 0x1900000;
std::vector<uint32_t> mem_a(0x1000), mem_b(0x1000), smem_a(0x1000), smem_b(0x1000);
bool reader_fn(uintptr_t at, void* out, size_t bytes) {
    struct Area { uintptr_t base; std::vector<uint32_t>* mem; };
    for (const Area& area : {Area{base_a, &mem_a}, Area{base_b, &mem_b}, Area{sbase_a, &smem_a}, Area{sbase_b, &smem_b}}) {
        if (at >= area.base && at + bytes <= area.base + area.mem->size() * 4) {
            std::memcpy(out, area.mem->data() + (at - area.base) / 4, bytes);
            return true;
        }
    }
    return false;
}
int main(int, char** argv) {
    for (size_t i = 0; i < mem_a.size(); ++i) { mem_a[i] = mem_b[i] = uint32_t(i * 3 + 1); smem_a[i] = smem_b[i] = uint32_t(i * 5 + 7); }
    // View: eye-specific matrix words, a rect x that differs, a scalar the reviewer must chase.
    mem_b[0x320 / 4] = 0x3f0a0000; mem_b[0x324 / 4] = 0xbf000000;
    mem_b[0x2f8 / 4] = 1;
    // State: a time-like float pair 3.25 s apart, a pointer-like qword (low half differs, high half equal), a flag.
    float t0 = 1234.5f, t1 = 1231.25f; std::memcpy(&smem_a[0x1c0 / 4], &t0, 4); std::memcpy(&smem_b[0x1c0 / 4], &t1, 4);
    smem_a[0x40 / 4] = 0x8bfe9970; smem_b[0x40 / 4] = 0x8bfeb2d0; smem_a[0x44 / 4] = smem_b[0x44 / 4] = 0x1a;
    smem_a[0x300 / 4] = 0; smem_b[0x300 / 4] = 1;
    auto read = [](uintptr_t at, auto& out) { return reader_fn(at, &out, sizeof(out)); };

    auto p = std::make_unique<Probe>();
    std::ofstream out(argv[1], std::ios::binary);
    out << Json{{"type", "header"}, {"version", 1}, {"seconds", 30}}.dump() << '\n';
    uint64_t now = 100000; uint64_t token = 0;
    for (uint32_t frame : {60u, 120u, 130u}) {
        ++token;
        View a{base_a, {base_a, true}, {frame, true}}, b{base_b, {base_b, true}, {frame, true}};
        a.state.value = sbase_a; b.state.value = sbase_b;
        for (uint32_t phase : {1u, 2u, 3u}) {
            Rec r{0x1234, phase};
            if (phase == 2) { a.frame.value = b.frame.value = frame + 1; }
            // -------- verbatim pair() call site --------
            ''' + site + r'''
            // --------------------------------------------
            now += 8;
        }
    }
    wuwa_eye_diff::Sample popped;
    int written = 0;
    while (p->eye_diff_ring.pop(popped)) { out << eye_diff_json(popped).dump() << '\n'; ++written; }
    // the sampler must have kept only the on-interval pair (frame 60) and 120: two pairs, phase 1+2 each
    std::cout << "written=" << written << '\n';
    assert(written == 4);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as work:
    work = pathlib.Path(work)
    (work / 'harness.cpp').write_text(harness)
    build = subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1',
                            f'-I{args.json_include}', f'-I{utility}', str(work / 'harness.cpp'),
                            '-o', str(work / 'harness')], capture_output=True, text=True)
    if build.returncode:
        sys.exit('serializer/call-site compile failed:\n' + build.stdout + build.stderr)
    produced = work / 'produced.jsonl'
    run = subprocess.run([str(work / 'harness'), str(produced)], capture_output=True, text=True)
    if run.returncode:
        sys.exit('harness failed:\n' + run.stdout + run.stderr)
    if args.update:
        fixture.parent.mkdir(exist_ok=True)
        fixture.write_bytes(produced.read_bytes())
        print('fixture rewritten:', fixture)
    elif produced.read_bytes() != fixture.read_bytes():
        sys.exit('native serializer output differs from ' + str(fixture) + '; review, then rerun with --update')
    else:
        print('eye-diff serializer compiles and matches the committed fixture')
