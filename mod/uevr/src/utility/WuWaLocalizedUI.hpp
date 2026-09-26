#pragma once
#include "WuWaLocalization.hpp"
#include "Mod.hpp"
#include <cstdarg>
#include <cstdio>
#include <algorithm>

namespace wuwa_ui {
struct FontScope {
    bool pushed=false;
    FontScope() { if(auto* font=wuwa_l10n::menu_font()) { ImGui::PushFont(font); pushed=true; } }
    ~FontScope() { if(pushed) ImGui::PopFont(); }
};
inline std::string formatted(const char* fmt,va_list args) {
    const auto translated=wuwa_l10n::translate(fmt);
    va_list copy; va_copy(copy,args);
    const int count=std::vsnprintf(nullptr,0,translated.c_str(),copy); va_end(copy);
    if(count<0 || count>32768) return fmt;
    std::string result(static_cast<size_t>(count)+1,'\0');
    std::vsnprintf(result.data(),result.size(),translated.c_str(),args); result.resize(count);
    return wuwa_l10n::translate(result);
}
// Wrap logical Arabic first, then shape/reorder each visible line. Letting
// ImGui wrap an already-reordered paragraph would reverse the line order.
inline std::string display(std::string_view logical,float width=0) {
    if(!wuwa_l10n::right_to_left() || width<=0) return wuwa_l10n::visual(logical);
    std::string output,line;
    size_t start=0;
    while(start<logical.size()) {
        const auto end=logical.find_first_of(" \n",start);
        const auto word=logical.substr(start,end==std::string_view::npos ? end : end-start);
        const auto next=line.empty() ? std::string(word) : line+" "+std::string(word);
        const auto rendered=wuwa_l10n::visual(next);
        if(!line.empty() && ImGui::CalcTextSize(rendered.c_str()).x>width) {
            output+=wuwa_l10n::visual(line)+"\n"; line=word;
        } else line=next;
        if(end==std::string_view::npos) break;
        if(logical[end]=='\n') { output+=wuwa_l10n::visual(line)+"\n"; line.clear(); }
        start=end+1;
    }
    return output+wuwa_l10n::visual(line);
}
inline void TextWrapped(const char* fmt,...) {
    va_list args; va_start(args,fmt); const auto logical=formatted(fmt,args); va_end(args);
    const auto value=display(logical,ImGui::GetContentRegionAvail().x);
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(value.c_str()); ImGui::PopTextWrapPos();
}
inline void Text(const char* fmt,...) {
    va_list args; va_start(args,fmt); const auto logical=formatted(fmt,args); va_end(args);
    const auto value=display(logical); ImGui::TextUnformatted(value.c_str());
}
inline void TextColored(const ImVec4& color,const char* fmt,...) {
    va_list args; va_start(args,fmt); const auto logical=formatted(fmt,args); va_end(args);
    const auto value=display(logical,ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_Text,color); ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str()); ImGui::PopTextWrapPos(); ImGui::PopStyleColor();
}
inline void TextUnformatted(const char* text) { const auto value=wuwa_l10n::text(text); ImGui::TextUnformatted(value.c_str()); }
inline bool Button(const char* text,const ImVec2& size=ImVec2{}) {const auto value=wuwa_l10n::label(text); return ImGui::Button(value.c_str(),size);}
inline bool TreeNode(const char* text) {const auto value=wuwa_l10n::label(text); return ImGui::TreeNode(value.c_str());}
inline bool CollapsingHeader(const char* text,ImGuiTreeNodeFlags flags=0) {const auto value=wuwa_l10n::label(text); return ImGui::CollapsingHeader(value.c_str(),flags);}
inline void TableSetupColumn(const char* text,ImGuiTableColumnFlags flags=0,float width=0,ImGuiID id=0) {const auto value=wuwa_l10n::label(text); ImGui::TableSetupColumn(value.c_str(),flags,width,id);}
inline bool Combo(const char* text,int* current,const char* const* items,int count,int height=-1) {
    std::vector<std::string> translated; std::vector<const char*> pointers;
    translated.reserve(count); pointers.reserve(count);
    for(int i=0;i<count;++i) translated.push_back(wuwa_l10n::text(items[i]));
    for(const auto& value:translated) pointers.push_back(value.c_str());
    const auto label=wuwa_l10n::label(text);
    return ImGui::Combo(label.c_str(),current,pointers.data(),count,height);
}
inline bool draw(IModValue& value,const char* text) {
    if(auto* combo=dynamic_cast<ModCombo*>(&value)) {
        if(!combo->should_draw_option() || combo->options().empty()) return false;
        const int count=static_cast<int>(combo->options().size());
        combo->value()=std::clamp(combo->value(),0,count-1);
        ImGui::PushID(combo);
        const bool changed=Combo(text,&combo->value(),combo->options().data(),count);
        combo->context_menu_logic(); ImGui::PopID(); return changed;
    }
    const auto label=wuwa_l10n::label(text); return value.draw(label);
}
}
