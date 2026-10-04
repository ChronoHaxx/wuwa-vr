#include "../mod/uevr/src/utility/WuWaControlRecovery.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace wuwa_controls;
    const Settings all{{"WuWaControls_Camera", "1"}, {"UI_Size", "1.3"},
        {"WuWaControls_ResetHudAspect", "true"}, {"WuWaControls_HudAspectStatus", "Refresh failed"},
        {"WuWaControls_NativeMenu", "true"}, {"VR_2DScreenMode", "true"}};
    const Settings expected{{"WuWaControls_Camera", "1"}, {"UI_Size", "1.3"}};
    Recovery recovery;
    recovery.remember_launch(all); recovery.supplied(all); recovery.remember_before_reset(all);
    assert(recovery.launch()==expected && recovery.supplied()==expected && recovery.undo()==expected);
    for (const auto& [key,value] : recovery.launch()) { assert(valid_value_like(value,value)); }
    assert(!valid_value_like("1", "Refresh failed"));
    recovery.finish_undo(); assert(!recovery.can_undo());
    std::cout << "Transient HUD actions excluded from launch, supplied and undo recovery\n";
}
