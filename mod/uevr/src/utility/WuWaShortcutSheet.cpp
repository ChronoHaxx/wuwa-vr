#define NOMINMAX
#include "WuWaShortcutSheet.hpp"
#include "WuWaLocalization.hpp"
#include "uevr-imgui/font_robotomedium.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

namespace wuwa_sheet {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT art_width = 768, art_height = 512;
std::vector<unsigned char> pixels;
ImFontAtlas* owner{};
ImFont* sheet_font{};
int rect_index{-1};
ImVec2 uv_min{}, uv_max{};
bool ready{};
std::string last_status{"not initialized"};

bool decode(HMODULE module) {
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(4815), MAKEINTRESOURCEW(10));
    if (!resource) { last_status="FindResource error "+std::to_string(GetLastError()); return false; }
    const auto bytes = SizeofResource(module, resource);
    const auto handle = LoadResource(module, resource);
    auto* data = handle ? static_cast<unsigned char*>(LockResource(handle)) : nullptr;
    if (!data || !bytes) { last_status="empty resource"; return false; }
    const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) { last_status="COM initialization "+std::to_string(init); return false; }
    struct ComScope { bool owned; ~ComScope() { if (owned) CoUninitialize(); } } com{SUCCEEDED(init)};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    const auto failed=[](HRESULT hr,const char* step) { if (FAILED(hr)) last_status=std::string(step)+" "+std::to_string(hr); return FAILED(hr); };
    if (failed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),"WIC factory") ||
        failed(factory->CreateStream(&stream),"stream") || failed(stream->InitializeFromMemory(data, bytes),"resource stream") ||
        failed(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),"PNG decoder") ||
        failed(decoder->GetFrame(0, &frame),"PNG frame") || failed(factory->CreateBitmapScaler(&scaler),"scaler") ||
        failed(scaler->Initialize(frame.Get(), art_width, art_height, WICBitmapInterpolationModeFant),"scale") ||
        failed(factory->CreateFormatConverter(&converter),"converter") ||
        failed(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom),"RGBA conversion")) return false;
    pixels.resize(art_width * art_height * 4);
    if (FAILED(converter->CopyPixels(nullptr, art_width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
        last_status="pixel copy"; pixels.clear(); return false;
    }
    return true;
}
}

void reserve_artwork(ImFontAtlas* atlas, HMODULE module) {
    ready = false; rect_index = -1; owner = atlas;
    sheet_font=atlas->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data,RobotoMedium_compressed_size,32.0f);
    if (pixels.empty() && !decode(module)) return;
    // ImGui's glyph-only width heuristic does not include custom rectangles.
    atlas->TexDesiredWidth=(std::max)(atlas->TexDesiredWidth,1024);
    rect_index = atlas->AddCustomRectRegular(art_width, art_height);
}

bool fill_artwork(ImFontAtlas* atlas) {
    ready = false;
    if (atlas != owner || rect_index < 0 || pixels.empty()) return false;
    const auto* rect = atlas->GetCustomRectByIndex(rect_index);
    if (!rect || !rect->IsPacked()) { last_status="artwork rectangle did not pack"; return false; }
    unsigned char* texture{}; int width{}, height{};
    atlas->GetTexDataAsRGBA32(&texture, &width, &height);
    if (!texture || rect->X + art_width > static_cast<UINT>(width) || rect->Y + art_height > static_cast<UINT>(height)) return false;
    for (UINT y = 0; y < art_height; ++y)
        std::memcpy(texture + ((rect->Y + y) * width + rect->X) * 4, pixels.data() + y * art_width * 4, art_width * 4);
    atlas->CalcCustomRectUV(rect, &uv_min, &uv_max);
    ready = true;
    last_status="ready";
    return true;
}
const char* artwork_status() { return last_status.c_str(); }

void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected) {
    if (owner == ImGui::GetIO().Fonts && sheet_font) font=sheet_font;
    if (auto* localized=wuwa_l10n::sheet_font()) font=localized;
    const float scale=(std::min)(size.x/1600.0f,size.y/900.0f);
    const ImVec2 origin{(size.x-1600.0f*scale)*0.5f,(size.y-900.0f*scale)*0.5f};
    const auto pt=[&](float x,float y) { return ImVec2{origin.x+x*scale,origin.y+y*scale}; };
    const auto centered=[&](float y,float px,ImU32 color,const char* text) {
        const auto value=wuwa_l10n::text(text);
        const float initial=font->CalcTextSizeA(px*scale,10000.0f,0.0f,value.c_str()).x;
        const float fitted=px*scale*(std::min)(1.0f,1350.0f*scale/(std::max)(initial,1.0f));
        const float w=font->CalcTextSizeA(fitted,10000.0f,0.0f,value.c_str()).x;
        draw->AddText(font,fitted,{size.x*0.5f-w*0.5f,origin.y+y*scale},color,value.c_str());
    };
    draw->PushClipRect(pt(0,0),pt(1600,900),true);
    if (!menu_detected) {
        // Controller-operated menus may never show the game's mouse cursor.
        // Keep a smaller recovery hint while HUD hiding is active, so absence
        // of that cursor signal cannot strand the player in a blurred menu.
        draw->AddRectFilled(pt(175,535),pt(1425,680),IM_COL32(55,8,12,232),18*scale);
        centered(552,42,IM_COL32(255,100,104,255),"GAME UI HIDDEN - MENUS ARE HIDDEN TOO");
        centered(609,34,IM_COL32(255,255,255,255),shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT > WuWa Controls > Show game UI now");
        draw->PopClipRect(); return;
    }
    draw->AddRectFilled(pt(100,230),pt(1500,670),IM_COL32(68,8,12,250),28*scale);
    draw->AddRect(pt(100,230),pt(1500,670),IM_COL32(255,65,72,255),28*scale,0,5*scale);
    centered(269,84,IM_COL32(255,90,96,255),"GAME UI IS HIDDEN");
    centered(373,39,IM_COL32(255,235,236,255),"Show UI to see this menu");
    centered(454,52,IM_COL32(255,255,255,255),shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT  =  OPEN UEVR");
    centered(523,28,IM_COL32(255,206,210,255),shortcut_available ? "L3 means click and hold the left stick, then press B." : "WuWa Controls > Show game UI now");
    centered(601,26,IM_COL32(255,206,210,255),shortcut_available ? "Or: UEVR > WuWa Controls > Show game UI now" : "Polar shortcuts are disabled or bypassed.");
    draw->PopClipRect();
}

void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu) {
    if (owner == ImGui::GetIO().Fonts && sheet_font) font=sheet_font;
    if (auto* localized=wuwa_l10n::sheet_font()) font=localized;
    const float scale=(std::min)(size.x/1600.0f,size.y/900.0f);
    const ImVec2 origin{(size.x-1600.0f*scale)*0.5f,(size.y-900.0f*scale)*0.5f};
    const float top=hidden_menu ? 700.0f : 590.0f;
    const auto pt=[&](float x,float y) { return ImVec2{origin.x+x*scale,origin.y+y*scale}; };
    const auto centered=[&](float y,float px,ImU32 color,const char* text) {
        const auto value=wuwa_l10n::text(text);
        const float initial=font->CalcTextSizeA(px*scale,10000.0f,0.0f,value.c_str()).x;
        const float fitted=px*scale*(std::min)(1.0f,1350.0f*scale/(std::max)(initial,1.0f));
        const float w=font->CalcTextSizeA(fitted,10000.0f,0.0f,value.c_str()).x;
        draw->AddText(font,fitted,{size.x*0.5f-w*0.5f,origin.y+y*scale},color,value.c_str());
    };
    draw->PushClipRect(pt(0,0),pt(1600,900),true);
    draw->AddRectFilled(pt(90,top),pt(1510,top+180),IM_COL32(43,31,7,246),18*scale);
    draw->AddRect(pt(90,top),pt(1510,top+180),IM_COL32(255,192,60,255),18*scale,0,3*scale);
    centered(top+15,43,IM_COL32(255,208,99,255),"HUD / MOUSE MODE ON");
    centered(top+70,36,IM_COL32(255,255,255,255),adjustment ? "L3 + LB  =  RETURN TO GAME CONTROLS" : "UEVR > WuWa Controls > Exit mouse mode now");
    centered(top+124,27,IM_COL32(245,224,181,255),adjustment ? "Click left stick + left bumper, then release all controls." : "Activated automatically in this menu: cursor only. L3 + R3 opens settings.");
    draw->PopClipRect();
}

void draw(ImDrawList* draw, ImVec2 size, int page, ImFont* font, int flight_style, bool adjusting, bool mouse_active, bool ui_hidden) {
    // One bounded layout shared by the VR sheet and the offscreen preview check.
    // Text is code-rendered: the generated image never supplies shortcut labels.
    page = std::clamp(page, 0, 3);
    if (owner == ImGui::GetIO().Fonts && sheet_font) font=sheet_font;
    if (auto* localized=wuwa_l10n::sheet_font()) font=localized;
    const float s = (std::min)(size.x / 1600.0f, size.y / 900.0f);
    const ImVec2 origin{(size.x - 1600.0f*s)*0.5f, (size.y - 900.0f*s)*0.5f};
    const auto pt = [&](float x, float y) { return ImVec2{origin.x + x*s, origin.y + y*s}; };
    constexpr ImU32 ink=IM_COL32(233,241,247,255), muted=IM_COL32(166,186,204,255), mint=IM_COL32(112,225,197,255);
    const auto text = [&](float x, float y, float px, ImU32 color, const char* label, float width=0.0f) {
        const auto value=wuwa_l10n::text(label);
        if(width<=0) width=(y>=790 || y==107) ? 1530-x : x>=1044 ? 1535-x : x>=718 ? 1535-x : 660-x;
        const float measured=font->CalcTextSizeA(px*s,10000.0f,0.0f,value.c_str()).x;
        const float fitted=px*s*(std::min)(1.0f,width*s/(std::max)(measured,1.0f));
        draw->AddText(font, fitted, pt(x,y), color, value.c_str());
    };
    const auto panel = [&](float x, float y, float w, float h) {
        draw->AddRectFilled(pt(x,y),pt(x+w,y+h),IM_COL32(18,31,46,255),18*s);
        draw->AddRect(pt(x,y),pt(x+w,y+h),IM_COL32(49,71,89,255),18*s);
    };
    draw->PushClipRect(pt(0,0),pt(1600,900),true);
    draw->AddRectFilled(pt(12,12),pt(1588,888),IM_COL32(8,17,28,252),24*s);
    draw->AddRectFilled(pt(44,47),pt(51,110),mint,3*s);
    text(72,41,42,ink,"WUWA VR");
    text(74,91,22,muted,"XBOX CONTROLLER  /  POLAR SHORTCUTS");
    constexpr const char* pages[]{"01  EVERYDAY", "02  CAMERAS", "03  MENUS & HUD", "04  UEVR SETTINGS"};
    text(1130,66,27,mint,pages[page]);
    if (ui_hidden) {
        draw->AddRectFilled(pt(718,103),pt(1556,136),IM_COL32(105,16,22,255),6*s);
        text(736,107,23,IM_COL32(255,229,229,255),"GAME UI HIDDEN: L3 + B restores the HUD and game menus.");
    }
    panel(44,144,650,524);
    text(70,166,25,mint,"KNOW YOUR BUTTONS");
    if (ready && owner == ImGui::GetIO().Fonts) {
        draw->AddImage(owner->TexID,pt(85,229),pt(661,613),uv_min,uv_max);
        const auto label = [&](float x, float y, const char* name, float tx, float ty) {
            const float w = font->CalcTextSizeA(21*s,10000,0,name).x/s + 22;
            draw->AddLine(pt(x+w*0.5f,y+27),pt(tx,ty),mint,1.5f*s);
            draw->AddCircleFilled(pt(tx,ty),3*s,mint);
            draw->AddRectFilled(pt(x,y),pt(x+w,y+29),IM_COL32(8,17,28,245),6*s);
            text(x+11,y+2,21,ink,name);
        };
        label(106,211,"LT / LB",204,264); label(494,211,"RT / RB",536,264);
        label(70,395,"L3",221,371); label(511,565,"R3",449,439);
        label(251,514,"View",331,359); label(352,201,"Menu",416,359);
    } else {
        text(100,307,28,ink,"L3 = click left stick");
        text(100,355,28,ink,"R3 = click right stick");
        text(100,415,25,muted,"View: two squares   Menu: three lines");
    }
    text(76,625,23,muted,"L3 / R3 = click the left / right stick");
    panel(718,144,838,524);
    const auto row = [&](int n, const char* buttons, const char* action) {
        const float y = 214.0f + n * 53.0f;
        if ((n % 2) == 0) draw->AddRectFilled(pt(738,y-5),pt(1535,y+42),IM_COL32(24,41,56,255),7*s);
        text(752,y,25,mint,buttons,276); text(1044,y,25,ink,action,480);
    };
    if (page == 0) {
        text(752,165,26,mint,"QUICK ACTIONS");
        row(0,"L3 + View","First person on / off");
        row(1,"L3 + RB","Game / fixed camera");
        row(2,"Double R3","Freecam on / off");
        row(3,"Double L3","Windows screenshot");
        row(4,"L3 + Y / X","First / fixed: height");
        row(5,"LB + LT / RT","Fixed: farther / closer");
        row(6,"L3 + LB","Toggle HUD / mouse mode");
        row(7,"L3 + D-pad Down","First person: animation on/off");
    } else if (page == 1) {
        text(752,165,26,mint,flight_style==3 ? "CAMERA TOOLS / ACRO DRONE" : flight_style==2 ? "CAMERA TOOLS / PLANE FPV" : flight_style==1 ? "CAMERA TOOLS / HOVER DRONE" : "CAMERA TOOLS");
        if (flight_style==3) {
            row(0,"Right stick","Roll / pitch (forward = down)");
            row(1,"Left stick X","Yaw");
            row(2,"RT / left Y","Throttle: choose in UEVR");
            row(3,"Tap RB","Arm at low throttle / disarm");
            row(4,"Hold LB","Pause and disarm");
            row(5,"LB + RB","Level drone and stop");
            row(6,"Double R3","Exit freecam");
            row(7,"Center sticks","Stop rotation; no auto-level");
        } else {
        row(0,"L3 + View","First person on / off");
        row(1,"L3 + Y / X","Height: first / fixed");
        row(2,"LB + LT / RT","Fixed: farther / closer");
        row(3,"Double R3","Freecam on / off");
        if (flight_style==2) {
            row(4,"Right stick / left X","Pitch + bank / yaw");
            row(5,"RT / LT","Cruise faster / slower");
            row(6,"LB / RB","Hold brake / boost");
            row(7,"L3 + A","Recenter view / portal");
        } else {
            row(4,"Left / right stick",flight_style==1 ? "Horizontal move / look" : "Freecam: move / look");
            row(5,"LT / LB","Freecam: rise / descend");
            row(6,"RT / double RT","Freecam: boost / turbo");
            row(7,"L3 + A","Recenter view / portal");
        }
        }
    } else if (page == 2) {
        const bool automatic=mouse_active && !adjusting;
        text(752,157,26,mint,adjusting || mouse_active ? "HUD / MOUSE MODE: ON" : "HUD / MOUSE MODE: OFF");
        if (automatic) text(752,188,18,muted,"Activated automatically in this menu: cursor only.");
        row(0,automatic ? "L3 + R3" : "L3 + LB, then release",automatic ? "Settings: Exit mouse mode now" : "Toggle this mode on / off");
        row(1,"Left stick","Move cursor");
        row(2,"A / B","Click / go back");
        row(3,"X + left stick","Scroll");
        row(4,"D-pad","Move map");
        row(5,"LT / RT",automatic ? "HUD: enter adjustment first" : "HUD nearer / farther");
        row(6,"LB / RB",automatic ? "HUD: enter adjustment first" : "HUD down / up");
        row(7,automatic ? "L3 + LB" : "Mode OFF",automatic ? "Enter HUD adjustment" : "Normal game/menu controls");
    } else {
        text(752,165,26,mint,"REFERENCE: OPEN UEVR TO USE THESE");
        row(0,"L3 + R3","Open / close UEVR settings");
        row(1,"D-pad / A / B","Navigate / activate / back");
        row(2,"LB / RB","Change sidebar page");
        row(3,"Left stick (RT off)","Scroll focused panel");
        row(4,"RT + left / right stick","Camera forward/side / height");
        row(5,"RT + D-pad L / R","Choose RT shortcut page");
        row(6,"RT page 1: B / Y / X","Reset offsets / view / origin");
        row(7,"RT page 2 / 3","X/Y/B: load / save slots 0/1/2");
    }
    // Essentials remain visible even when automatic pages select camera/menu help.
    panel(44,692,1512,144);
    const auto essential=[&](float x,const char* buttons,const char* action) {
        text(x,706,26,mint,buttons,348); text(x,742,24,ink,action,348);
    };
    essential(72,"L3 + B","Show / hide game UI");
    essential(452,"L3 + A","Recenter view / portal");
    essential(832,"L3 + R3","UEVR settings");
    essential(1212,"L3 + Menu","Show / hide this sheet");
    text(72,779,22,muted,"Portal: L3 + LT / F7  |  Diorama: L3 + RT  |  LT + RT, then click L3 = stereo, R3 = mono.",1458);
    text(72,810,19,muted,"Sheet: L3 + D-pad Left/Right = pages; Up = automatic   |   UEVR: release RT to scroll.",1458);
    if (adjusting || mouse_active) draw->AddRectFilled(pt(44,842),pt(1556,880),IM_COL32(63,43,8,255),6*s);
    text(64,853,20,(adjusting || mouse_active) ? IM_COL32(255,215,117,255) : muted,
        adjusting ? "HUD / MOUSE ON: gameplay input paused. L3 + LB exits; release all controls. UEVR settings still open with L3 + R3."
        : mouse_active ? "HUD / MOUSE ON (menu cursor): L3 + R3 > WuWa Controls > Exit mouse mode now restores game controls."
        : "L3/R3 = stick clicks. L3 + LB toggles HUD/mouse adjustment. Utility: LB + R3 = V; hold 0.8 s for Tab wheel.");
    draw->PopClipRect();
}
}
