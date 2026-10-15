#pragma once

// "Save changes for next time" (WuWa VR menu, Menu page; this launch only).
// Off: Framework::save_config writes nothing, so what is tried now lasts until
// the game closes; turning it back on saves the current settings at the next
// save (every menu open and close).
#include <atomic>

namespace wuwa_config_save {
inline std::atomic<bool> paused{false};
} // namespace wuwa_config_save
