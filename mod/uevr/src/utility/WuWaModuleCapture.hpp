#pragma once

// Opt-in, read-only diagnostic for this WuWa build. Place
// wuwa-module-capture.request.json beside the loaded backend DLL with
// {"output":"E:/Coding/wuwa-vr/extracted/lgui-live/<unique>.bin"}.
// The output directory must already exist. Files are never overwritten.
// Remove the request after use; it is examined once per backend load.
// Add "any_build": true to capture a game build other than the pinned one (for
// porting offsets after a game update). Still read-only; the identity is recorded.
// Optional: "skip": [[rva, rva_end], ...] never touches those spans; "only": [[...]]
// captures just those spans; "checkpoint_every_chunk": true publishes progress after
// every 1 MiB so a read that never returns can be located and skipped next time.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace wuwa_module_capture {
namespace detail {
using json = nlohmann::json;
namespace fs = std::filesystem;
using Range = std::array<uint64_t, 2>;
constexpr DWORD expected_timestamp = 0x6a74963e;
constexpr DWORD expected_image_size = 1029861376;
constexpr DWORD expected_header_size = 1536;
constexpr size_t chunk_size = 1024 * 1024;
constexpr uint64_t checkpoint_bytes = 16 * 1024 * 1024;
constexpr ULONGLONG checkpoint_interval_ms = 5000;

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE handle) : value{handle} {}
    ~Handle() { close(); }
    void close() noexcept {
        if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

inline void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

inline std::string utc_now() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

inline fs::path module_path(HMODULE module) {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(length != 0 && length < path.size(), "GetModuleFileNameW failed");
    path.resize(length);
    return fs::path{path};
}

// Convert common read faults to missing spans without C++ objects to unwind.
// VirtualQuery and the copy are not atomic; process-specific exception handlers
// and process termination remain outside the guarantees of this guard.
__declspec(noinline) inline bool guarded_copy(void* destination, const void* source, size_t length) {
    __try {
        std::memcpy(destination, source, length);
        return true;
    } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                 GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                 GetExceptionCode() == EXCEPTION_GUARD_PAGE) ?
                    EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

inline bool readable(const MEMORY_BASIC_INFORMATION& region, HMODULE executable) {
    if (region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0 ||
        region.AllocationBase != executable) return false;
    switch (region.Protect & 0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}

inline bool read_checked(void* destination, uintptr_t source, size_t length, HMODULE executable) {
    size_t copied{};
    while (copied < length) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(source + copied), &region, sizeof(region)) != sizeof(region) ||
            !readable(region, executable)) return false;
        const auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (end <= source + copied) return false;
        const auto count = (std::min)(length - copied, end - source - copied);
        if (!guarded_copy(static_cast<char*>(destination) + copied,
                reinterpret_cast<void*>(source + copied), count)) return false;
        copied += count;
    }
    return true;
}

struct PE {
    IMAGE_NT_HEADERS64 nt{};
    LONG offset{};
    std::vector<IMAGE_SECTION_HEADER> sections{};
};

inline PE parse_pe(const std::vector<char>& bytes) {
    require(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "Missing DOS header");
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, bytes.data(), sizeof(dos));
    require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= sizeof(dos), "Invalid DOS header");
    PE pe{};
    pe.offset = dos.e_lfanew;
    require(static_cast<size_t>(pe.offset) + sizeof(pe.nt) <= bytes.size(), "Missing NT headers");
    std::memcpy(&pe.nt, bytes.data() + pe.offset, sizeof(pe.nt));
    require(pe.nt.Signature == IMAGE_NT_SIGNATURE && pe.nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        pe.nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "Expected AMD64 PE32+ executable");
    require(pe.nt.FileHeader.NumberOfSections > 0 && pe.nt.FileHeader.NumberOfSections <= 96 &&
        pe.nt.FileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64), "Invalid PE section table");
    const auto table = pe.offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + pe.nt.FileHeader.SizeOfOptionalHeader;
    require(table + pe.nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) <= bytes.size(), "Truncated PE section table");
    require(pe.nt.OptionalHeader.SizeOfImage > 0 && pe.nt.OptionalHeader.SizeOfImage <= 2ull * 1024 * 1024 * 1024 &&
        pe.nt.OptionalHeader.SizeOfHeaders >= table + pe.nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) &&
        pe.nt.OptionalHeader.SizeOfHeaders <= bytes.size() &&
        pe.nt.OptionalHeader.SizeOfHeaders <= pe.nt.OptionalHeader.SizeOfImage, "Invalid PE image/header size");
    pe.sections.resize(pe.nt.FileHeader.NumberOfSections);
    std::memcpy(pe.sections.data(), bytes.data() + table, pe.sections.size() * sizeof(IMAGE_SECTION_HEADER));
    for (const auto& section : pe.sections) {
        const uint64_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        require(section.VirtualAddress + size <= pe.nt.OptionalHeader.SizeOfImage, "Section exceeds image size");
    }
    return pe;
}

inline json describe_pe(const PE& pe) {
    json result{{"timestamp", pe.nt.FileHeader.TimeDateStamp}, {"preferred_base", pe.nt.OptionalHeader.ImageBase},
        {"image_size", pe.nt.OptionalHeader.SizeOfImage}, {"header_size", pe.nt.OptionalHeader.SizeOfHeaders},
        {"machine", pe.nt.FileHeader.Machine}, {"section_count", pe.nt.FileHeader.NumberOfSections},
        {"pe_header_offset", pe.offset}, {"sections", json::array()}};
    for (const auto& section : pe.sections) {
        const std::string name{reinterpret_cast<const char*>(section.Name), strnlen(reinterpret_cast<const char*>(section.Name), 8)};
        result["sections"].push_back({{"name", name}, {"rva", section.VirtualAddress},
            {"virtual_size", section.Misc.VirtualSize}, {"raw_size", section.SizeOfRawData},
            {"raw_offset", section.PointerToRawData}, {"flags", section.Characteristics}});
    }
    return result;
}

inline void validate_identity(const PE& pe, bool any_build) {
    if (any_build) return; // porting capture: identity is recorded in the metadata instead
    require(pe.nt.FileHeader.TimeDateStamp == expected_timestamp, "Unexpected WuWa PE timestamp");
    require(pe.nt.OptionalHeader.SizeOfImage == expected_image_size &&
        pe.nt.OptionalHeader.SizeOfHeaders == expected_header_size, "Unexpected WuWa PE image/header size");
}

inline void add_range(std::vector<Range>& ranges, uint64_t begin, uint64_t end) {
    if (begin == end) return;
    ranges.push_back({begin, end});
    std::sort(ranges.begin(), ranges.end());
    size_t count{};
    for (const auto& range : ranges) {
        if (count > 0 && ranges[count - 1][1] >= range[0]) {
            ranges[count - 1][1] = (std::max)(ranges[count - 1][1], range[1]);
        } else {
            ranges[count++] = range;
        }
    }
    ranges.resize(count);
}

// Spans minus the excluded ranges (both lists of [begin, end)).
inline std::vector<Range> without(const std::vector<Range>& spans, const std::vector<Range>& excluded) {
    std::vector<Range> result{};
    for (const auto& span : spans) {
        std::vector<Range> parts{span};
        for (const auto& cut : excluded) {
            std::vector<Range> next{};
            for (const auto& part : parts) {
                if (cut[1] <= part[0] || cut[0] >= part[1]) { next.push_back(part); continue; }
                if (cut[0] > part[0]) next.push_back({part[0], cut[0]});
                if (cut[1] < part[1]) next.push_back({cut[1], part[1]});
            }
            parts = std::move(next);
        }
        result.insert(result.end(), parts.begin(), parts.end());
    }
    return result;
}

inline std::vector<Range> missing_ranges(uint64_t begin, uint64_t end, const std::vector<Range>& captured) {
    std::vector<Range> missing{};
    for (const auto& range : captured) {
        if (range[1] <= begin) continue;
        if (range[0] >= end) break;
        if (range[0] > begin) missing.push_back({begin, (std::min)(range[0], end)});
        begin = (std::max)(begin, (std::min)(range[1], end));
    }
    if (begin < end) missing.push_back({begin, end});
    return missing;
}

inline void seek(HANDLE file, uint64_t offset) {
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    require(SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != 0, "Output seek failed");
}

inline void write(HANDLE file, const void* bytes, size_t length) {
    DWORD written{};
    require(length <= MAXDWORD && WriteFile(file, bytes, static_cast<DWORD>(length), &written, nullptr) &&
        written == length, "Output write failed");
}

inline void write_json(HANDLE file, const json& metadata) {
    const auto text = metadata.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
    write(file, text.data(), text.size());
    require(FlushFileBuffers(file) != 0, "Cannot flush capture metadata");
}

inline void publish_checkpoint(const fs::path& destination, const json& metadata) {
    const auto temporary = fs::path{destination.wstring() + L".tmp"};
    {
        Handle file{CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        require(file.value != INVALID_HANDLE_VALUE && GetFileType(file.value) == FILE_TYPE_DISK,
            "Cannot create checkpoint temporary file");
        write_json(file.value, metadata);
    }
    // No REPLACE_EXISTING: completed checkpoints are immutable. An interrupted
    // temporary file never becomes an apparently complete checkpoint.
    require(MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
        "Cannot publish capture checkpoint");
}

inline void describe_coverage(json& metadata, const PE& live, const std::vector<Range>& ranges) {
    metadata["captured_ranges"] = ranges;
    metadata["missing_ranges"] = missing_ranges(0, live.nt.OptionalHeader.SizeOfImage, ranges);
    uint64_t captured_bytes{};
    for (const auto& range : ranges) captured_bytes += range[1] - range[0];
    metadata["captured_bytes"] = captured_bytes;
    metadata["header_complete"] = missing_ranges(0, live.nt.OptionalHeader.SizeOfHeaders, ranges).empty();
    bool sections_complete = true;
    metadata["section_coverage"] = json::array();
    for (const auto& section : live.sections) {
        const uint64_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        const auto missing = missing_ranges(section.VirtualAddress, section.VirtualAddress + size, ranges);
        sections_complete = sections_complete && missing.empty();
        const std::string name{reinterpret_cast<const char*>(section.Name), strnlen(reinterpret_cast<const char*>(section.Name), 8)};
        metadata["section_coverage"].push_back({{"name", name}, {"rva", section.VirtualAddress},
            {"size", size}, {"missing_ranges", missing}});
    }
    metadata["analysis_ready"] = metadata["header_complete"].get<bool>() && sections_complete;
}

inline fs::path output_path(const fs::path& requested) {
    const fs::path allowed{L"E:\\Coding\\wuwa-vr\\extracted\\lgui-live"};
    require(requested.is_absolute() && requested.extension() == L".bin", "Output must be an absolute .bin path");
    const auto output = requested.lexically_normal().make_preferred();
    // A single file directly in the known directory keeps the request interface
    // narrow. Reject alternate streams and reparse points in its ancestry.
    require(_wcsicmp(output.parent_path().c_str(), allowed.c_str()) == 0 &&
        output.filename().wstring().find(L':') == std::wstring::npos, "Output must be directly under extracted/lgui-live");
    const auto stem = output.stem().wstring();
    require(!stem.empty() && stem.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::wstring::npos,
        "Output filename must use letters, digits, hyphens or underscores");
    for (auto directory = allowed; !directory.empty() && directory != directory.root_path(); directory = directory.parent_path()) {
        const auto attributes = GetFileAttributesW(directory.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "Output directory must exist without reparse points");
    }
    return output;
}

struct Work {
    fs::path request{};
    HMODULE backend{};
    std::shared_ptr<spdlog::logger> logger{};
};

inline void capture(const Work& work) {
    std::ifstream request{work.request, std::ios::binary};
    require(request.good() && fs::file_size(work.request) <= 4096, "Invalid capture request");
    const auto config = json::parse(request);
    const auto output = output_path(fs::u8path(config.at("output").get<std::string>()));
    const bool any_build = config.value("any_build", false);
    const bool every_chunk = config.value("checkpoint_every_chunk", false);
    std::vector<Range> excluded{};
    const auto read_ranges = [&](const char* key) {
        std::vector<Range> list{};
        if (config.contains(key)) {
            for (const auto& item : config.at(key)) {
                const auto a = item.at(0).get<uint64_t>(), b = item.at(1).get<uint64_t>();
                require(a < b, "Capture request range must be [begin, end) with begin < end");
                list.push_back({a, b});
            }
            std::sort(list.begin(), list.end());
        }
        return list;
    };
    excluded = read_ranges("skip");
    if (const auto only = read_ranges("only"); !only.empty()) {
        uint64_t position{};
        for (const auto& span : only) {
            if (span[0] > position) excluded.push_back({position, span[0]});
            position = (std::max)(position, span[1]);
        }
        excluded.push_back({position, UINT64_MAX});
    }
    const auto companion = fs::path{output.wstring() + L".json"};
    const auto pending_companion = fs::path{companion.wstring() + L".pending"};
    // Reserve this capture without publishing an empty final JSON. Interrupted
    // .pending/.tmp files are not complete metadata; prior checkpoints survive.
    require(!fs::exists(output) && !fs::exists(companion), "Capture output already exists");
    Handle metadata_file{CreateFileW(pending_companion.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(metadata_file.value != INVALID_HANDLE_VALUE && GetFileType(metadata_file.value) == FILE_TYPE_DISK, "Cannot reserve companion metadata");
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);
    const uint64_t capture_ticks = (static_cast<uint64_t>(file_time.dwHighDateTime) << 32) | file_time.dwLowDateTime;
    json metadata{{"format", "wuwa-mapped-module-v1"}, {"any_build", any_build}, {"excluded_ranges", excluded}, {"layout", "memory"}, {"offset_equals_rva", true},
        {"pid", GetCurrentProcessId()}, {"started_utc", utc_now()}, {"status", "failed"},
        {"capture_id", fmt::format("{}-{}", GetCurrentProcessId(), capture_ticks)},
        {"phase", "initializing"}, {"checkpoint_sequence", 0}, {"attempted_range", nullptr},
        {"analysis_ready", false}, {"header_complete", false}, {"atomic_snapshot", false},
        {"image", output.string()}, {"module_resolution", "in_process_GetModuleHandleW"},
        {"capture_method", "VirtualQuery_and_guarded_direct_copy"}, {"regions", json::array()},
        {"captured_ranges", json::array()}, {"read_errors", json::array()},
        {"hole_policy", "Uncaptured bytes are zero-filled placeholders. Consult captured_ranges."}};
    Handle image{INVALID_HANDLE_VALUE};
    std::optional<PE> live{};
    std::vector<Range> ranges{};
    std::vector<Range> durable_ranges{};
    try {
        const auto executable = GetModuleHandleW(nullptr);
        const auto executable_path = module_path(executable);
        require(_wcsicmp(executable_path.filename().c_str(), L"Client-Win64-Shipping.exe") == 0, "Unexpected executable name");
        std::ifstream disk{executable_path, std::ios::binary};
        require(disk.good(), "Cannot read executable headers from disk");
        std::vector<char> disk_bytes(chunk_size);
        disk.read(disk_bytes.data(), disk_bytes.size());
        disk_bytes.resize(static_cast<size_t>(disk.gcount()));
        const auto pe = parse_pe(disk_bytes);
        metadata["disk"] = describe_pe(pe);
        const uint64_t image_size = pe.nt.OptionalHeader.SizeOfImage;
        const uint64_t header_size = pe.nt.OptionalHeader.SizeOfHeaders;
        const auto base = reinterpret_cast<uintptr_t>(executable);
        metadata["module"] = {{"base", base}, {"size", image_size}, {"path", executable_path.string()}, {"name", executable_path.filename().string()}};
        std::vector<char> live_headers(static_cast<size_t>(header_size));
        require(read_checked(live_headers.data(), base, live_headers.size(), executable), "Cannot read live PE headers");
        live = parse_pe(live_headers);
        metadata["live"] = describe_pe(*live);
        // The running executable legitimately changes section metadata. Capture
        // its live layout while checking the observed image's stable identity.
        validate_identity(pe, any_build);
        validate_identity(*live, any_build);
        metadata["live_header_validated"] = true;
        image.value = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(image.value != INVALID_HANDLE_VALUE && GetFileType(image.value) == FILE_TYPE_DISK, "Cannot create capture image");
        seek(image.value, image_size);
        require(SetEndOfFile(image.value) != 0, "Cannot size capture image");
        work.logger->info("[WuWaDiag] module capture started: base={:x} size={} output={}", base, image_size, output.string());
        work.logger->flush();
        std::vector<char> buffer(chunk_size);
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        uint64_t copied_bytes{};
        uint64_t last_checkpoint_bytes{};
        ULONGLONG last_checkpoint_tick = GetTickCount64();
        uint64_t sequence{};
        const auto checkpoint = [&](bool force) {
            const auto now = GetTickCount64();
            if (!force && !every_chunk && copied_bytes - last_checkpoint_bytes < checkpoint_bytes &&
                now - last_checkpoint_tick < checkpoint_interval_ms) return;
            // A published checkpoint describes only successful writes confirmed
            // by a flush. Later phases never overwrite these captured intervals.
            require(FlushFileBuffers(image.value) != 0, "Cannot flush image before checkpoint");
            durable_ranges = ranges;
            describe_coverage(metadata, *live, durable_ranges);
            metadata["status"] = "capturing";
            metadata["checkpoint_sequence"] = ++sequence;
            metadata["checkpoint_utc"] = utc_now();
            const auto checkpoint_name = fs::path{output.wstring() +
                fs::path{fmt::format(".checkpoint-{:06}.json", sequence)}.wstring()};
            publish_checkpoint(checkpoint_name, metadata);
            last_checkpoint_bytes = copied_bytes;
            last_checkpoint_tick = GetTickCount64();
            work.logger->info("[WuWaDiag] module capture checkpoint: phase={} attempted={} captured={} read_errors={} file={}",
                metadata["phase"].get<std::string>(), metadata["attempted_range"].dump(),
                metadata["captured_bytes"].get<uint64_t>(), metadata["read_errors"].size(), checkpoint_name.string());
            work.logger->flush();
        };

        // Reuse the bytes that passed live PE validation, then durably publish
        // their provenance before touching the rest of the image.
        metadata["phase"] = "headers";
        metadata["attempted_range"] = Range{0, header_size};
        seek(image.value, 0);
        write(image.value, live_headers.data(), live_headers.size());
        add_range(ranges, 0, header_size);
        copied_bytes += header_size;
        checkpoint(true);

        const auto run_phase = [&](const char* phase, const std::vector<Range>& requested) {
            metadata["phase"] = phase;
            metadata["attempted_range"] = nullptr;
            work.logger->info("[WuWaDiag] module capture phase: {}", phase);
            work.logger->flush();
            for (const auto& requested_range : requested) {
                require(requested_range[0] <= requested_range[1] && requested_range[1] <= image_size,
                    "Capture phase extends beyond executable image");
                // Ranges stay normalized even when phases visit high RVAs first.
                // Subtract successful prior writes, preserving old checkpoints.
                const auto pending = without(missing_ranges(requested_range[0], requested_range[1], ranges), excluded);
                for (const auto& span : pending) {
                    auto cursor = span[0];
                    while (cursor < span[1]) {
                        MEMORY_BASIC_INFORMATION region{};
                        require(VirtualQuery(reinterpret_cast<void*>(base + cursor), &region, sizeof(region)) == sizeof(region), "VirtualQuery failed");
                        const auto region_end = std::min<uint64_t>(span[1], reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize - base);
                        require(region_end > cursor && reinterpret_cast<uintptr_t>(region.BaseAddress) <= base + cursor, "Invalid memory region");
                        const bool can_read = readable(region, executable);
                        metadata["regions"].push_back({{"phase", phase}, {"rva", cursor}, {"size", region_end - cursor},
                            {"state", region.State}, {"protect", region.Protect}, {"type", region.Type}, {"readable", can_read}});
                        if (!can_read) {
                            metadata["attempted_range"] = Range{cursor, region_end};
                            cursor = region_end;
                            checkpoint(false);
                            continue;
                        }
                        while (cursor < region_end) {
                            const auto length = std::min<uint64_t>(chunk_size, region_end - cursor);
                            metadata["attempted_range"] = Range{cursor, cursor + length};
                            const auto copy_span = [&](uint64_t start, size_t size) {
                                if (!read_checked(buffer.data(), base + start, size, executable)) return false;
                                seek(image.value, start);
                                write(image.value, buffer.data(), size);
                                add_range(ranges, start, start + size);
                                copied_bytes += size;
                                return true;
                            };
                            if (!copy_span(cursor, static_cast<size_t>(length))) {
                                metadata["read_errors"].push_back({{"phase", phase}, {"rva", cursor}, {"size", length}, {"retry", "page_reads"}});
                                for (auto page = cursor; page < cursor + length;) {
                                    const auto size = std::min<uint64_t>(system.dwPageSize - (base + page) % system.dwPageSize, cursor + length - page);
                                    metadata["attempted_range"] = Range{page, page + size};
                                    if (!copy_span(page, static_cast<size_t>(size))) {
                                        metadata["read_errors"].push_back({{"phase", phase}, {"rva", page}, {"size", size}, {"retry", nullptr}});
                                    }
                                    page += size;
                                    checkpoint(false);
                                }
                            }
                            cursor += length;
                            checkpoint(false);
                        }
                    }
                }
            }
            checkpoint(true);
        };

        std::vector<Range> exception_ranges{};
        if (live->nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
            const auto& exception = live->nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            metadata["live"]["exception_directory"] = {{"rva", exception.VirtualAddress}, {"size", exception.Size}};
            if (exception.Size != 0) {
                exception_ranges.push_back({exception.VirtualAddress, static_cast<uint64_t>(exception.VirtualAddress) + exception.Size});
            }
        }
        run_phase("exception_directory", exception_ranges);
        std::vector<Range> readonly_ranges{};
        std::vector<Range> executable_ranges{};
        for (const auto& section : live->sections) {
            const uint64_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
            const Range span{section.VirtualAddress, section.VirtualAddress + size};
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) executable_ranges.push_back(span);
            else if ((section.Characteristics & IMAGE_SCN_MEM_READ) != 0 &&
                (section.Characteristics & IMAGE_SCN_MEM_WRITE) == 0) readonly_ranges.push_back(span);
        }
        run_phase("readonly_data", readonly_ranges);
        run_phase("executable", executable_ranges);
        run_phase("remaining", std::vector<Range>{{0, image_size}});
        metadata["phase"] = "complete";
        metadata["status"] = metadata["missing_ranges"].empty() ? "complete" : "partial";
    } catch (const std::exception& error) {
        // Preserve only flushed successful ranges even if a C++ error stops a
        // phase. Abrupt process exit still leaves earlier immutable snapshots.
        if (image.value != INVALID_HANDLE_VALUE && image.value != nullptr) {
            if (FlushFileBuffers(image.value)) durable_ranges = ranges;
            else metadata["final_image_flush_error"] = GetLastError();
        }
        if (live) describe_coverage(metadata, *live, durable_ranges);
        metadata["status"] = "failed";
        metadata["analysis_ready"] = false;
        metadata["error"] = error.what();
    }
    metadata["finished_utc"] = utc_now();
    write_json(metadata_file.value, metadata);
    metadata_file.close();
    require(MoveFileExW(pending_companion.c_str(), companion.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
        "Cannot publish final capture metadata");
    work.logger->info("[WuWaDiag] module capture finished: status={} output={} error={}",
        metadata["status"].get<std::string>(), output.string(), metadata.value("error", std::string{}));
    work.logger->flush();
}

inline DWORD WINAPI worker(void* argument) {
    auto* raw = static_cast<Work*>(argument);
    const auto backend = raw->backend;
    {
        std::unique_ptr<Work> work{raw};
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        try { capture(*work); }
        catch (const std::exception& error) { work->logger->error("[WuWaDiag] module capture rejected: {}", error.what()); }
        catch (...) { work->logger->error("[WuWaDiag] module capture failed with unknown exception"); }
    }
    // The worker owns no Framework/VR objects. Keep its code loaded through the
    // final instruction even if the user unloads the backend during capture.
    FreeLibraryAndExitThread(backend, 0);
}
} // namespace detail

inline void start_if_requested(HMODULE backend) noexcept {
    static std::atomic<bool> checked{false};
    if (checked.exchange(true)) return;
    try {
        const auto request = detail::module_path(backend).parent_path() / L"wuwa-module-capture.request.json";
        if (!std::filesystem::exists(request)) return;
        auto work = std::make_unique<detail::Work>();
        work->request = request;
        work->logger = spdlog::default_logger();
        HMODULE retained{};
        detail::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&detail::worker), &retained) != 0, "Cannot retain backend for capture worker");
        work->backend = retained;
        const auto thread = CreateThread(nullptr, 0, detail::worker, work.get(), 0, nullptr);
        if (thread == nullptr) {
            FreeLibrary(retained);
            throw std::runtime_error("Cannot create module capture worker");
        }
        work.release();
        CloseHandle(thread);
    } catch (const std::exception& error) {
        spdlog::error("[WuWaDiag] module capture startup rejected: {}", error.what());
    } catch (...) {
        spdlog::error("[WuWaDiag] module capture startup failed");
    }
}
} // namespace wuwa_module_capture
