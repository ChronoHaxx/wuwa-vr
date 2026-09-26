// Real D3D11 WARP readback -> WIC PNG -> WIC decode. No runtime or headset.
#define NOMINMAX
#include "capture_image.hpp"
#include <iostream>

using capture::check;
using Microsoft::WRL::ComPtr;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static capture::Image gpu_image(ID3D11Device* device, ID3D11DeviceContext* context, bool bgra) {
    // Unequal R/B values, two rows and deliberately padded input stride.
    const uint8_t data[] = {9,22,57,0, 1,2,3,0, 201,202,203,204,
                           90,21,75,0, 4,5,6,0, 211,212,213,214};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 2; desc.Height = 2; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA initial{data, 12, 0};
    ComPtr<ID3D11Texture2D> source, staging;
    check(device->CreateTexture2D(&desc, &initial, &source));
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc, nullptr, &staging));
    context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    auto image = capture::pixels(mapped, 2, 2, bgra);
    context->Unmap(staging.Get(), 0);
    return image;
}
int main() {
    try {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context));
        const auto left = gpu_image(device.Get(), context.Get(), false);
        const auto right = gpu_image(device.Get(), context.Get(), true);
        const std::vector<uint8_t> expected = {
            9,22,57,255, 1,2,3,255, 57,22,9,255, 3,2,1,255,
            90,21,75,255, 4,5,6,255, 75,21,90,255, 6,5,4,255};
        const auto pair = capture::side_by_side(left, right);
        require(pair.rgba == expected, "Row stride, channel order or eye placement changed");
        require(capture::has_color(left), "Nonblack image rejected");
        require(!capture::has_color({1,1,{0,0,0,255}}), "Opaque black image accepted");
        require(capture::advanced(1, 0xffffffffu), "Frame wrap rejected");
        require(!capture::advanced(100, 101), "Frame reset accepted");
        require(!capture::advanced(12, 12), "Stalled frame accepted");
        const auto folder = std::filesystem::temp_directory_path() /
            (L"wuwa-capture-test-\u65e5\u672c-" + std::to_wstring(GetCurrentProcessId()));
        require(std::filesystem::create_directory(folder), "Test output exists");
        const auto path = folder / L"\u773c.png";
        capture::png(path, pair);
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
        check(decoder->GetFrame(0, &frame));
        UINT width{}, height{};
        check(frame->GetSize(&width, &height));
        require(width == 4 && height == 2, "PNG dimensions differ");
        check(factory->CreateFormatConverter(&converter));
        check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<uint8_t> decoded(32);
        check(converter->CopyPixels(nullptr, 16, 32, decoded.data()));
        require(decoded == expected, "PNG roundtrip changed RGB or eye placement");
        converter.Reset(); frame.Reset(); decoder.Reset(); factory.Reset();
        std::filesystem::remove(path); std::filesystem::remove(folder);
        try { capture::side_by_side(left, {1,1,{0,0,0,255}}); throw std::logic_error("Unequal eyes accepted"); }
        catch (const std::runtime_error&) {}
        try { capture::pixels({nullptr,4,0}, 2, 2, false); throw std::logic_error("Invalid readback accepted"); }
        catch (const std::runtime_error&) {}
        CoUninitialize();
        std::cout << "WARP/WIC pixel roundtrip and freshness checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
