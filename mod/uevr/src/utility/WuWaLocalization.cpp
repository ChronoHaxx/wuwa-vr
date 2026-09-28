#define NOMINMAX
#include "WuWaLocalization.hpp"
#include "uevr-imgui/font_robotomedium.hpp"
#include <nlohmann/json.hpp>
#include <icu.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace wuwa_l10n {
namespace {
using Json=nlohmann::json;
std::recursive_mutex mutex;
Json bundled=Json::object();
std::map<std::string,std::string> strings;
std::unordered_map<std::string,std::string> shaped;
std::vector<Language> choices{{"en","English"}};
std::string active="en", requested="en", font_kind="latin", message;
std::filesystem::path override_dir;
bool pending=true, initialized=false, rtl=false;
ImFontAtlas* font_owner{};
ImFont *menu{}, *sheet{};
ImVector<ImWchar> glyph_ranges;

struct Icu {
    HMODULE module=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    decltype(&u_shapeArabic) shape=module ? reinterpret_cast<decltype(shape)>(GetProcAddress(module,"u_shapeArabic")) : nullptr;
    decltype(&ubidi_open) open=module ? reinterpret_cast<decltype(open)>(GetProcAddress(module,"ubidi_open")) : nullptr;
    decltype(&ubidi_close) close=module ? reinterpret_cast<decltype(close)>(GetProcAddress(module,"ubidi_close")) : nullptr;
    decltype(&ubidi_setPara) paragraph=module ? reinterpret_cast<decltype(paragraph)>(GetProcAddress(module,"ubidi_setPara")) : nullptr;
    decltype(&ubidi_writeReordered) reorder=module ? reinterpret_cast<decltype(reorder)>(GetProcAddress(module,"ubidi_writeReordered")) : nullptr;
    bool ready() const { return shape && open && close && paragraph && reorder; }
    ~Icu() { if(module) FreeLibrary(module); }
};
Icu& icu() { static Icu api; return api; }

std::string_view resource(HMODULE module,int id) {
    const auto found=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    const auto handle=found ? LoadResource(module,found) : nullptr;
    const auto bytes=handle ? static_cast<const char*>(LockResource(handle)) : nullptr;
    return bytes ? std::string_view{bytes,SizeofResource(module,found)} : std::string_view{};
}
bool safe_id(std::string_view id) {
    return !id.empty() && id.size()<25 && std::all_of(id.begin(),id.end(),[](unsigned char c) {
        return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-';
    });
}
bool utf8(std::string_view value) {
    return value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0;
}
std::optional<std::vector<std::string>> formats(std::string_view value) {
    std::vector<std::string> out;
    for(size_t i=0;i<value.size();++i) {
        if(value[i]!='%') continue;
        if(++i>=value.size()) return std::nullopt;
        if(value[i]=='%') continue;
        while(i<value.size() && std::string_view("-+ #0.123456789").find(value[i])!=std::string_view::npos) ++i;
        // No positional arguments, dynamic widths, or write-through specifiers.
        const size_t start=i;
        while(i<value.size() && std::string_view("hljztL").find(value[i])!=std::string_view::npos) ++i;
        if(i==value.size() || std::string_view("diuoxXfFeEgGaAcsp").find(value[i])==std::string_view::npos) return std::nullopt;
        out.emplace_back(value.substr(start,i-start+1));
    }
    return out;
}
bool metadata(const Json& data,std::string_view id) {
    if(!data.is_object() || !data.contains("schema") || !data["schema"].is_number_integer() || data["schema"]!=1 ||
        !data.contains("language") || !data["language"].is_string() || data["language"].get_ref<const std::string&>()!=id ||
        !data.contains("strings") || !data["strings"].is_object() || data["strings"].size()>1200) return false;
    for(const char* key : {"name","direction","font"}) {
        if(!data.contains(key)) continue;
        if(!data[key].is_string()) return false;
        const auto& value=data[key].get_ref<const std::string&>();
        if(value.size()>80 || !utf8(value) || value.find('\0')!=std::string::npos || value.find("##")!=std::string::npos) return false;
    }
    if(data.contains("direction") && data["direction"]!="ltr" && data["direction"]!="rtl") return false;
    if(data.contains("font") && data["font"]!="latin" && data["font"]!="sc" && data["font"]!="jp" && data["font"]!="kr" && data["font"]!="arabic") return false;
    return true;
}
void merge_strings(const Json& data) {
    for(const auto& [key,value] : data["strings"].items()) {
        if(!value.is_string()) continue;
        const auto translation=value.get<std::string>();
        if(valid_translation(key,translation)) strings[key]=translation;
    }
}
Json read_override(const std::filesystem::path& path) {
    std::error_code error;
    if(!std::filesystem::is_regular_file(path,error) || std::filesystem::file_size(path,error)>512*1024 || error) return {};
    std::ifstream stream{path,std::ios::binary};
    return Json::parse(stream,nullptr,false);
}
std::string shape_line(std::string_view value) {
    if(!rtl || !icu().ready() || value.empty()) return std::string(value);
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0 || count>8192) return std::string(value);
    std::vector<UChar> input(count), arabic(count*2+32), output(count*2+32);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),reinterpret_cast<wchar_t*>(input.data()),count);
    UErrorCode err=U_ZERO_ERROR;
    const int shaped_length=icu().shape(input.data(),count,arabic.data(),static_cast<int>(arabic.size()),U_SHAPE_LETTERS_SHAPE,&err);
    if(U_FAILURE(err)) return std::string(value);
    auto* bidi=icu().open();
    if(!bidi) return std::string(value);
    icu().paragraph(bidi,arabic.data(),shaped_length,UBIDI_DEFAULT_LTR,nullptr,&err);
    const int written=U_SUCCESS(err) ? icu().reorder(bidi,output.data(),static_cast<int>(output.size()),UBIDI_DO_MIRRORING|UBIDI_REMOVE_BIDI_CONTROLS,&err) : 0;
    icu().close(bidi);
    if(U_FAILURE(err) || written<=0) return std::string(value);
    const auto* wide=reinterpret_cast<const wchar_t*>(output.data());
    std::string result(WideCharToMultiByte(CP_UTF8,0,wide,written,nullptr,0,nullptr,nullptr),'\0');
    WideCharToMultiByte(CP_UTF8,0,wide,written,result.data(),static_cast<int>(result.size()),nullptr,nullptr);
    return result;
}
}

bool valid_translation(std::string_view source,std::string_view translated) {
    if(source.empty() || source.size()>8192 || translated.empty() || translated.size()>8192 ||
        source.find('\0')!=std::string_view::npos || translated.find('\0')!=std::string_view::npos ||
        translated.find("##")!=std::string_view::npos || !utf8(source) || !utf8(translated)) return false;
    const auto a=formats(source), b=formats(translated);
    return a && b && *a==*b;
}
void request(std::string_view id,const std::filesystem::path& path,bool reload) {
    std::scoped_lock lock{mutex};
    const auto next=safe_id(id) ? std::string(id) : "en";
    if(reload || next!=requested || path!=override_dir) {
        requested=next; override_dir=path; pending=true;
    }
}
bool apply_pending(HMODULE module) {
    std::scoped_lock lock{mutex};
    if(!pending) return false;
    pending=false; message.clear();
    try {
    if(!initialized) {
        const auto bytes=resource(module,4830);
        if(!bytes.empty()) bundled=Json::parse(bytes,nullptr,false);
        if(!bundled.is_object()) bundled=Json::object();
        initialized=true;
    }
    strings.clear(); shaped.clear(); active=requested; choices.clear();
    for(const auto& [id,data] : bundled.items())
        if(safe_id(id) && metadata(data,id)) choices.push_back({id,data.value("name",id)});
    if(choices.empty()) choices.push_back({"en","English"});
    Json selected=bundled.contains(active) ? bundled[active] : Json{};
    if(!override_dir.empty()) {
        std::error_code error; size_t count=0;
        for(std::filesystem::directory_iterator it{override_dir,error},end; !error && it!=end && count<32; it.increment(error),++count) {
            const auto id=it->path().stem().string();
            if(it->path().extension()!=L".json" || !safe_id(id)) continue;
            const auto data=read_override(it->path());
            if(!metadata(data,id)) continue;
            if(std::none_of(choices.begin(),choices.end(),[&](const Language& x){return x.id==id;})) choices.push_back({id,data.value("name",id)});
            if(id==active) {
                if(metadata(selected,id)) {
                    merge_strings(selected);
                    // A partial community override must retain its script font
                    // and direction unless the file explicitly changes them.
                    auto merged=data;
                    for(const char* key : {"name","direction","font"})
                        if(!merged.contains(key) && selected.contains(key)) merged[key]=selected[key];
                    selected=std::move(merged);
                    continue;
                }
                selected=data;
            }
        }
    }
    if(!metadata(selected,active)) {
        active="en"; selected=bundled.value("en",Json{});
        message="Language file unavailable; English fallback is active.";
    }
    if(!selected.is_object()) selected=Json::object();
    rtl=selected.value("direction",std::string{"ltr"})=="rtl";
    if(rtl && !icu().ready()) {
        active="en"; selected=bundled.value("en",Json{}); rtl=false; strings.clear();
        message="Windows ICU is unavailable; English fallback avoids broken Arabic text.";
    }
    font_kind=selected.value("font",std::string{"latin"});
    if(metadata(selected,active)) merge_strings(selected);
    std::sort(choices.begin(),choices.end(),[](const Language& a,const Language& b) {return a.id!=b.id && (a.id=="en" || (b.id!="en" && a.name<b.name));});
    } catch(const std::exception&) {
        // Editable text must not let malformed filesystem/catalog data escape
        // into the render loop. English source strings remain usable.
        active="en"; rtl=false; font_kind="latin"; strings.clear(); shaped.clear();
        choices={{"en","English"}};
        message="Language reload failed; English fallback is active.";
    }
    return true;
}
std::string language() { std::scoped_lock lock{mutex}; return active; }
std::string status() { std::scoped_lock lock{mutex}; return message; }
std::vector<Language> languages() { std::scoped_lock lock{mutex}; return choices; }
bool rtl_available() { return icu().ready(); }
bool right_to_left() { std::scoped_lock lock{mutex}; return rtl; }
std::string translate(std::string_view english) {
    std::scoped_lock lock{mutex};
    const auto found=strings.find(std::string(english));
    return found!=strings.end() ? found->second : std::string(english);
}
std::string visual(std::string_view logical) {
    std::scoped_lock lock{mutex};
    if(!rtl) return std::string(logical);
    const std::string key{logical};
    if(const auto found=shaped.find(key);found!=shaped.end()) return found->second;
    std::string result; size_t start=0;
    do {
        const size_t end=logical.find('\n',start);
        result+=shape_line(logical.substr(start,end==std::string_view::npos ? end : end-start));
        if(end==std::string_view::npos) break;
        result+='\n'; start=end+1;
    } while(start<logical.size());
    if(shaped.size()>=4096) shaped.clear();
    shaped.emplace(key,result); return result;
}
std::string text(std::string_view english) { return visual(translate(english)); }
std::string label(std::string_view english) { return text(english)+"###"+std::string(english); }
std::vector<std::string> translated_strings() {
    std::scoped_lock lock{mutex}; std::vector<std::string> out;
    for(const auto& [key,value] : strings) out.push_back(value);
    return out;
}
void reserve_fonts(ImFontAtlas* atlas,HMODULE module,float menu_size) {
    std::scoped_lock lock{mutex};
    font_owner=atlas; menu=sheet=nullptr;
    ImFontGlyphRangesBuilder ranges;
    ranges.AddRanges(atlas->GetGlyphRangesDefault());
    // Rebuilt from the current editable catalog: new words need no recompile.
    for(const auto& [key,value] : strings) {
        ranges.AddText(value.c_str()); ranges.AddText(visual(value).c_str());
    }
    glyph_ranges.clear();
    ranges.BuildRanges(&glyph_ranges);
    const auto latin=resource(module,4820);
    int extra_id=font_kind=="sc" ? 4821 : font_kind=="jp" ? 4822 : font_kind=="kr" ? 4823 : font_kind=="arabic" ? 4824 : 0;
    const auto extra=extra_id ? resource(module,extra_id) : std::string_view{};
    const auto add=[&](float size) {
        ImFontConfig cfg; cfg.FontDataOwnedByAtlas=false;
        auto* font=latin.empty() ? atlas->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data,RobotoMedium_compressed_size,size) :
            atlas->AddFontFromMemoryTTF(const_cast<char*>(latin.data()),static_cast<int>(latin.size()),size,&cfg,glyph_ranges.Data);
        if(!extra.empty()) {
            cfg.MergeMode=true;
            atlas->AddFontFromMemoryTTF(const_cast<char*>(extra.data()),static_cast<int>(extra.size()),size,&cfg,glyph_ranges.Data);
        }
        return font;
    };
    menu=add(menu_size); sheet=add(32.0f);
}
ImFont* menu_font() { return ImGui::GetCurrentContext() && font_owner==ImGui::GetIO().Fonts ? menu : nullptr; }
ImFont* sheet_font() { return ImGui::GetCurrentContext() && font_owner==ImGui::GetIO().Fonts ? sheet : nullptr; }
}
