#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace capture {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("HRESULT " + std::to_string(result));
}
struct Image {
    UINT width{}, height{};
    std::vector<uint8_t> rgba;
};
inline size_t image_bytes(UINT width, UINT height) {
    const auto bytes = uint64_t(width) * height * 4;
    if (!width || !height || width > 32768 || height > 16384 || bytes > 256ull * 1024 * 1024)
        throw std::runtime_error("Unsupported mirror dimensions");
    return static_cast<size_t>(bytes);
}
inline Image pixels(const D3D11_MAPPED_SUBRESOURCE& data, UINT width, UINT height, bool bgra) {
    Image image{width, height, std::vector<uint8_t>(image_bytes(width, height))};
    if (!data.pData || data.RowPitch < uint64_t(width) * 4)
        throw std::runtime_error("Invalid mirror readback stride");
    for (UINT y = 0; y < height; ++y) {
        const auto* src = static_cast<const uint8_t*>(data.pData) + size_t(y) * data.RowPitch;
        auto* dst = image.rgba.data() + size_t(y) * width * 4;
        for (UINT x = 0; x < width; ++x) {
            dst[x * 4] = src[x * 4 + (bgra ? 2 : 0)];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + (bgra ? 0 : 2)];
            dst[x * 4 + 3] = 255; // A composited display image; RGB remains unchanged.
        }
    }
    return image;
}
inline bool has_color(const Image& image) {
    for (size_t i = 0; i < image.rgba.size(); i += 4)
        if (image.rgba[i] || image.rgba[i + 1] || image.rgba[i + 2]) return true;
    return false;
}
inline Image side_by_side(const Image& left, const Image& right) {
    if (left.width != right.width || left.height != right.height ||
        left.rgba.size() != image_bytes(left.width, left.height) ||
        right.rgba.size() != image_bytes(right.width, right.height))
        throw std::runtime_error("Mirror dimensions differ between eyes");
    Image output{left.width * 2, left.height, std::vector<uint8_t>(image_bytes(left.width * 2, left.height))};
    const auto row = size_t(left.width) * 4;
    for (UINT y = 0; y < left.height; ++y) {
        std::memcpy(output.rgba.data() + size_t(y) * row * 2, left.rgba.data() + size_t(y) * row, row);
        std::memcpy(output.rgba.data() + size_t(y) * row * 2 + row, right.rgba.data() + size_t(y) * row, row);
    }
    return output;
}
inline void png(const std::filesystem::path& path, const Image& image) {
    if (image.rgba.size() != image_bytes(image.width, image.height))
        throw std::runtime_error("Invalid image buffer");
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    check(factory->CreateStream(&stream));
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(image.width, image.height));
    auto format = GUID_WICPixelFormat32bppRGBA;
    check(frame->SetPixelFormat(&format));
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppRGBA,
        image.width * 4, static_cast<UINT>(image.rgba.size()), const_cast<BYTE*>(image.rgba.data()), &bitmap));
    check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    check(frame->WriteSource(converter.Get(), nullptr));
    check(frame->Commit());
    check(encoder->Commit());
}
inline bool advanced(uint32_t current, uint32_t initial, uint32_t minimum = 2) {
    const auto delta = current - initial;
    return delta >= minimum && delta < 0x80000000u;
}
} // namespace capture
