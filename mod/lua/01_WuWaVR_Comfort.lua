-- Live layout controls for the existing UEVR camera and extracted LGUI quad.
-- No gameplay button remapping. HUD aspect refresh uses guarded LGUI reflection.
local vr = uevr.params.vr
local log = uevr.params.functions
local layout_file = "WuWaVR-layouts.json"
local layout_names = {"Gameplay", "Menu", "Dialogue"}
local groups = {
    input = {
        {"VR_ControllersAllowed", "Enable motion-controller input"},
    },
    visibility = {
        {"VR_EnableGUI", "Show game HUD and menus"},
    },
    camera = {
        {"VR_WorldScale", "World / tracking scale", 0.01, 10, 0.025},
        {"VR_CameraForwardOffset", "Camera forward / back", -4000, 4000, 1},
        {"VR_CameraRightOffset", "Camera right / left", -4000, 4000, 1},
        {"VR_CameraUpOffset", "Camera up / down", -4000, 4000, 1},
        {"VR_DecoupledPitch", "Keep headset pitch separate from game camera"},
        {"VR_DecoupledPitchUIAdjust", "Adjust HUD for decoupled pitch"},
    },
    rendering = {
        {"WindowMode_Enabled", "6DoF portal window (optional)"},
        {"VR_NativeStereoFix", "Native Stereo Fix (required for accepted material fixes)"},
    },
    hud = {
        {"UI_X_Offset", "HUD right / left (m)", -10, 10, 0.01},
        {"UI_Y_Offset", "HUD up / down (m)", -10, 10, 0.01},
        {"UI_Distance", "HUD distance (m)", 0.5, 10, 0.01},
        {"UI_Size", "HUD height (m)", 0.5, 10, 0.01},
        {"UI_FollowView", "HUD follows head direction"},
    },
}
local specs = {}
for _, group in pairs(groups) do
    for _, spec in ipairs(group) do specs[spec[1]] = spec end
end
groups.shortcuts = {{"VR_ToggleSlateGUIKey", "Game UI shortcut", -1, 255, 1}}
specs.VR_ToggleSlateGUIKey = groups.shortcuts[1]
local layouts = {version = 1}
local undo, last_written = {}, {}
local show_camera, show_hud = true, true
local message = "Layout controls ready; no settings changed."

local function report(value, failed)
    message = value
    if failed then log.log_error("WuWaComfort: " .. value)
    else log.log_info("WuWaComfort: " .. value) end
end

local function valid(spec, value)
    if spec[3] == nil then return type(value) == "boolean" end
    return type(value) == "number" and value == value
        and value >= spec[3] and value <= spec[4]
end

local function read(key)
    local ok, raw = pcall(function() return vr:get_mod_value(key) end)
    if not ok then return nil end
    local spec = specs[key]
    local value
    if spec[3] == nil then
        if raw == "true" or raw == "1" then value = true
        elseif raw == "false" or raw == "0" then value = false end
    else value = tonumber(raw) end
    if valid(spec, value) then return value end
    return nil
end

local function equal(a, b)
    if type(a) == "number" and type(b) == "number" then return math.abs(a - b) < 0.00001 end
    return a == b
end

local function write(key, value)
    if specs[key] == nil or not valid(specs[key], value) then return false end
    local ok = pcall(function() vr.set_mod_value(key, tostring(value)) end)
    return ok and equal(read(key), value)
end

local function change(key, value)
    local before = read(key)
    if before == nil then report(key .. " unavailable; left unchanged.", true); return end
    if write(key, value) then
        if undo[key] == nil then undo[key] = before end
        last_written[key] = value
        report(specs[key][2] .. " updated.")
    else
        -- Best-effort rollback of just this explicit edit, never a whole profile.
        write(key, before)
        report(key .. " did not read back correctly; restore attempted.", true)
    end
end

local function restore_group(name)
    local conflicts = 0
    for _, spec in ipairs(groups[name]) do
        local key = spec[1]
        if undo[key] ~= nil then
            if equal(read(key), last_written[key]) and write(key, undo[key]) then
                undo[key], last_written[key] = nil, nil
            else conflicts = conflicts + 1 end
        end
    end
    report(conflicts == 0 and "Restored this panel's " .. name .. " edits."
        or "Some settings changed elsewhere or could not restore; kept them unchanged.", conflicts > 0)
end

local function valid_layout(layout)
    if type(layout) ~= "table" then return false end
    for _, spec in ipairs(groups.hud) do
        if not valid(spec, layout[spec[1]]) then return false end
    end
    for key in pairs(layout) do
        local allowed = false
        for _, spec in ipairs(groups.hud) do if key == spec[1] then allowed = true end end
        if not allowed then return false end
    end
    return true
end

-- JSON is UEVR's profile-local Lua file API. Layouts contain only the five HUD
-- settings above; never replay arbitrary mod values from a file.
local function load_saved()
    -- First run has no bookmark file. Avoid logging a JSON parse error for
    -- that normal condition; UEVR resolves both APIs under profile/data.
    local file = io.open(layout_file, "r")
    if file == nil then return nil end
    file:close()
    return json.load_file(layout_file)
end
if json ~= nil then
    local ok, saved = pcall(load_saved)
    if ok and type(saved) == "table" and saved.version == 1 then
        for _, name in ipairs(layout_names) do
            if valid_layout(saved[name]) then layouts[name] = saved[name] end
        end
    end
end

local function save_layout(name)
    local values = {}
    for _, spec in ipairs(groups.hud) do
        local value = read(spec[1])
        if value == nil then report("HUD setting unavailable; layout not saved.", true); return end
        values[spec[1]] = value
    end
    layouts[name] = values
    if json ~= nil then
        local ok, result = pcall(json.dump_file, layout_file, layouts, 4)
        if ok and result ~= false then
            -- The backend's dump_file can return true even after a stream
            -- write fails, so check the persisted contents before claiming save.
            local read_ok, stored = pcall(load_saved)
            local matched = read_ok and type(stored) == "table" and stored.version == 1
            for _, slot in ipairs(layout_names) do
                if layouts[slot] ~= nil then
                    matched = matched and valid_layout(stored and stored[slot])
                    for _, spec in ipairs(groups.hud) do
                        matched = matched and equal(stored[slot][spec[1]], layouts[slot][spec[1]])
                    end
                end
            end
            if matched then report(name .. " HUD layout saved."); return end
        end
    end
    report(name .. " layout kept for this Lua session only; file save unavailable.", true)
end

local function apply_layout(name, layout)
    if not valid_layout(layout) then report("Save a " .. name .. " HUD layout first.", true); return end
    local before = {}
    for _, spec in ipairs(groups.hud) do
        before[spec[1]] = read(spec[1])
        if before[spec[1]] == nil then report("HUD setting unavailable; layout not changed.", true); return end
    end
    local written = {}
    for _, spec in ipairs(groups.hud) do
        local key = spec[1]
        written[#written + 1] = key
        if not write(key, layout[key]) then
            local restored = true
            for _, touched in ipairs(written) do restored = write(touched, before[touched]) and restored end
            report(restored and "Layout failed; previous HUD restored."
                or "Layout failed and restore was incomplete; check HUD controls.", true)
            return
        end
    end
    for key, value in pairs(before) do
        if undo[key] == nil then undo[key] = value end
        last_written[key] = layout[key]
    end
    report(name .. " HUD layout applied.")
end

local function use_layout(name)
    apply_layout(name, layouts[name])
end

-- Temporary menu comfort is deliberately opt-in for this Lua session. The
-- native flag is an expiring LGUI-render observation, NOT a cinematic signal.
-- Keep this ownership separate from explicit panel edits/bookmark undo.
local menu_layout_enabled, menu_visibility_enabled = false, false
local menu_owned, menu_releasing = nil, false
local menu_wait_clear, menu_clear_time = false, 0
local menu_status = "Off / 关闭"

local function status_bool(key)
    local ok, value = pcall(function() return vr:get_mod_value(key) end)
    if not ok then return nil end
    if value == "true" or value == "1" then return true end
    if value == "false" or value == "0" then return false end
    return nil
end

-- Native owns the short-lived presentation lease; the optional helper only
-- reports exact reflected movie/story signals from the game thread.
local auto_cinema = {tick = function() end, reset = function() end}
local cinema_attempted = false
-- UEVR adds this script's directory to package.path only while executing its
-- top-level chunk, then restores it before callbacks. Resolve the inert helper
-- now; its factory and all reflection/native work still wait for explicit ON.
-- Autoloading the helper as a standalone script does not populate require's cache.
local cinema_ok, cinema_module = pcall(require, "wuwa_auto_cinema")
local function cinema_tick()
    if not cinema_attempted and status_bool("VR_AutoCinema") == true then
        cinema_attempted = true
        if cinema_ok and type(cinema_module) == "table" and type(cinema_module.new) == "function" then
            local ready, instance = pcall(cinema_module.new, {api = uevr.api, types = uevr.types,
                get = function(key) return vr:get_mod_value(key) end,
                set = function(key, value) vr.set_mod_value(key, value) end,
                encode = function(value) return json.dump_string(value) end,
                log = function(text) log.log_info(text) end})
            if ready and type(instance) == "table" and type(instance.tick) == "function" and type(instance.reset) == "function" then
                auto_cinema = instance
            else
                log.log_error("WuWaCinema: optional detector initialization failed: " ..
                    (ready and "invalid helper instance" or tostring(instance)))
            end
        else
            log.log_error("WuWaCinema: optional detector module unavailable: " ..
                (cinema_ok and ("invalid helper module (" .. type(cinema_module) .. ")") or tostring(cinema_module)))
        end
    end
    auto_cinema:tick()
end

-- Companion module is inert until a manual request or an observed 2D transition.
local hud_refresh = {status = "HUD refresh module unavailable / HUD 刷新模块不可用"}
function hud_refresh:request() end
function hud_refresh:tick() end
function hud_refresh:reset() end
local hud_ok, hud_module = pcall(require, "wuwa_hud_refresh")
local hud_load_error
if hud_ok and type(hud_module) == "table" and type(hud_module.new) == "function" then
    local ready, instance = pcall(hud_module.new, {api = uevr.api, types = uevr.types,
        clock = function()
            local ok, value = pcall(function() return vr:get_mod_value("WuWaControls_Clock") end)
            return ok and tonumber(value) or nil
        end})
    if ready and type(instance) == "table" and type(instance.status) == "string"
        and type(instance.request) == "function" and type(instance.tick) == "function"
        and type(instance.reset) == "function" then
        hud_refresh = instance
    else
        hud_load_error = ready and "invalid helper instance" or tostring(instance)
    end
else
    hud_load_error = hud_ok and "invalid helper module" or tostring(hud_module)
end
if hud_load_error then
    hud_refresh.status = "HUD refresh module unavailable / HUD 刷新模块不可用: " .. hud_load_error
    log.log_error("WuWaComfort HUD module: " .. hud_load_error)
end

local function publish_hud_status()
    local status = hud_refresh.status
    if type(status) ~= "string" then return end
    local bounded = status
    if #bounded > 240 then
        local boundary = utf8.offset(bounded, 0, 241)
        bounded = bounded:sub(1, (boundary or 241) - 1) .. "..."
    end
    -- Older backends do not expose this transient status key.
    local ok, current = pcall(function() return vr:get_mod_value("WuWaControls_HudAspectStatus") end)
    if not ok or type(current) ~= "string" or current == "" then return end
    -- The native button replaces its status with "queued" before Lua handles
    -- the request. Republish unchanged failures too; otherwise a missing
    -- companion can appear queued forever. Matching readback stays write-free.
    if current == bounded then return end
    pcall(function() vr.set_mod_value("WuWaControls_HudAspectStatus", bounded) end)
end

local function menu_release(reason)
    menu_status = reason
    if not menu_owned then menu_releasing = false; return true end
    menu_releasing = true
    local remaining = false
    for key, owned in pairs(menu_owned) do
        local current = read(key)
        if current == nil then
            remaining = true -- An unavailable read is not proof we lost ownership.
        elseif not equal(current, owned.applied) then
            menu_owned[key] = nil -- Keep changes made by the user/another feature.
        elseif write(key, owned.before) then
            menu_owned[key] = nil
        else
            remaining = true -- Retry a failed restore on the next engine tick.
        end
    end
    if not remaining then menu_owned = nil; menu_releasing = false end
    if remaining then menu_status = "Restore pending; keep this script loaded / 等待恢复，请保留此脚本" end
    return not remaining
end

local function menu_enter()
    local desired, keys = {}, {}
    if menu_layout_enabled and valid_layout(layouts.Menu) then
        for _, spec in ipairs(groups.hud) do
            keys[#keys + 1] = spec[1]
            desired[spec[1]] = layouts.Menu[spec[1]]
        end
    end
    if menu_visibility_enabled then
        keys[#keys + 1] = "VR_EnableGUI"
        desired.VR_EnableGUI = true
    end
    if #keys == 0 then
        menu_status = "Save a Menu HUD first / 请先保存菜单 HUD 布局"
        menu_wait_clear = true
        return
    end
    -- Read the whole transaction before its first write.
    local before = {}
    for _, key in ipairs(keys) do
        before[key] = read(key)
        if before[key] == nil then
            menu_status = "HUD setting unavailable; unchanged / HUD 设置不可用，未修改"
            menu_wait_clear = true
            return
        end
    end
    menu_owned = {}
    for _, key in ipairs(keys) do
        if not equal(before[key], desired[key]) then
            -- Track the attempted write as well: a failed read-back can still
            -- mean the backend accepted the value and needs restoration.
            menu_owned[key] = {before = before[key], applied = desired[key]}
            if not write(key, desired[key]) then
                local actual = read(key)
                if actual ~= nil then menu_owned[key].applied = actual end
                menu_wait_clear = true
                menu_release("Menu HUD apply failed; previous values restored / 应用失败，已尝试恢复")
                return
            end
        end
    end
    menu_status = "Temporary menu HUD active / 临时菜单 HUD 已开启"
    if menu_layout_enabled and not valid_layout(layouts.Menu) then
        menu_status = "Temporary visibility only; save Menu HUD for placement / 仅临时显示界面，请保存菜单布局"
    end
end

local function menu_tick(delta)
    if menu_releasing then
        menu_release("Previous HUD restored / 已恢复之前的 HUD")
        return
    end
    if not menu_layout_enabled and not menu_visibility_enabled then
        menu_release("Off / 关闭")
        menu_wait_clear, menu_clear_time = false, 0
        return
    end
    local detected = status_bool("WuWaControls_NativeMenu")
    if detected == nil then
        menu_wait_clear, menu_clear_time = true, 0
        menu_release("Native menu signal unavailable; suspended / 菜单信号不可用，已暂停")
        return
    end
    -- The renderer already expires observations after 250 ms. Require another
    -- 200 ms of known false before leaving/rearming, avoiding a one-tick flicker.
    if not detected then
        delta = type(delta) == "number" and delta == delta and math.max(0, math.min(delta, 0.25)) or 0
        menu_clear_time = menu_clear_time + delta
        if menu_clear_time >= 0.2 then
            menu_release("Ready for the next native menu / 等待下次游戏菜单")
            menu_wait_clear = false
        end
        return
    end
    menu_clear_time = 0
    local ui_ok, drawing_ui = pcall(log.is_drawing_ui)
    if status_bool("WuWaControls_Focused") ~= true or
        status_bool("WuWaControls_AdjustMode") ~= false or not ui_ok or drawing_ui ~= false then
        menu_wait_clear = true
        menu_release("Paused for focus or manual adjustment; reopen the game menu / 焦点或手动调整期间暂停，请重新打开游戏菜单")
        return
    end
    if menu_wait_clear then return end
    if menu_owned then
        for key, owned in pairs(menu_owned) do
            if not equal(read(key), owned.applied) then
                menu_wait_clear = true
                menu_release("Manual HUD edit kept; reopen the game menu to resume / 已保留手动修改，重新打开游戏菜单后继续")
                return
            end
        end
    else
        menu_enter()
    end
end

local last_mono_theatre = nil
uevr.sdk.callbacks.on_pre_engine_tick(function(_, delta)
    cinema_tick()
    -- The native recovery button only posts a transient request; UObject work
    -- stays on this game-thread callback and never runs from the render UI.
    if status_bool("WuWaControls_ResetHudAspect") == true then
        local cleared = pcall(function() vr.set_mod_value("WuWaControls_ResetHudAspect", "false") end)
        if cleared and status_bool("WuWaControls_ResetHudAspect") == false then hud_refresh:request()
        else hud_refresh.status = "HUD request could not be acknowledged / 无法确认 HUD 刷新请求" end
    end
    local mono = status_bool("WuWaControls_EffectiveMonoTheatre")
    if mono == nil then mono = status_bool("VR_MonoTheatreMode") == true end
    -- Stereo screen -> mono can keep the same target dimensions. Refresh its
    -- canvas too; no ESC/menu input is needed, including during dialogue.
    if last_mono_theatre ~= nil and mono ~= last_mono_theatre then hud_refresh:request() end
    last_mono_theatre = mono
    local screen = status_bool("WuWaControls_EffectiveScreen")
    if screen == nil then screen = status_bool("VR_2DScreenMode") end
    hud_refresh:tick(mono or screen, delta)
    publish_hud_status()
    local ok, err = pcall(menu_tick, delta)
    if not ok then
        menu_wait_clear = true
        menu_release("Temporary menu HUD paused after an error / 临时菜单 HUD 出错后暂停")
        report("Temporary menu HUD: " .. tostring(err):sub(1, 160), true)
    end
end)

local function centered_preset(name, height, distance)
    -- OpenXR's UI_Size is quad HEIGHT; width follows the UI texture aspect.
    -- Preserve the chosen head-follow policy, rather than anchoring implicitly.
    local follow = read("UI_FollowView")
    if follow == nil then report("HUD follow setting unavailable; preset not applied.", true); return end
    apply_layout(name, {UI_X_Offset=0, UI_Y_Offset=0, UI_Distance=distance,
        UI_Size=height, UI_FollowView=follow})
end

local function earlier_camera()
    -- The Sep 20 accepted camera used 0.875, before the Sep 22 10x experiment.
    -- Change only scale and additive camera offsets; stereo and HUD stay put.
    local keys = {"VR_WorldScale", "VR_CameraForwardOffset", "VR_CameraRightOffset", "VR_CameraUpOffset"}
    local desired = {0.875, 0, 0, 0}
    local before = {}
    for i, key in ipairs(keys) do
        before[i] = read(key)
        if before[i] == nil then report("Camera setting unavailable; camera not changed.", true); return end
    end
    for i, key in ipairs(keys) do
        if not write(key, desired[i]) then
            local restored = true
            for j = 1, i do restored = write(keys[j], before[j]) and restored end
            report(restored and "Camera preset failed; previous camera restored."
                or "Camera preset failed; restore incomplete.", true)
            return
        end
    end
    for i, key in ipairs(keys) do
        if undo[key] == nil then undo[key] = before[i] end
        last_written[key] = desired[i]
    end
    report("Earlier camera scale restored (0.875x); camera offsets zeroed.")
end

local origin_undo, origin_written
local function vector_finite(v)
    for _, key in ipairs({"x", "y", "z"}) do
        local n = v[key]
        if type(n) ~= "number" or n ~= n or math.abs(n) == math.huge then return false end
    end
    return true
end
local function origin_read()
    local origin = UEVR_Vector3f.new()
    vr.get_standing_origin(origin)
    assert(vector_finite(origin), "Invalid standing origin")
    return origin
end
local function same_vector(a, b)
    return equal(a.x, b.x) and equal(a.y, b.y) and equal(a.z, b.z)
end
local function tracking_read()
    assert(vr.is_runtime_ready(), "VR runtime is not ready")
    assert(vr.is_hmd_active(), "Head tracking is not active")
    local head, rotation = UEVR_Vector3f.new(), UEVR_Quaternionf.new()
    vr.get_pose(vr.get_hmd_index(), head, rotation)
    assert(vector_finite(head) and vector_finite(rotation), "Invalid headset pose")
    local norm = rotation.x^2 + rotation.y^2 + rotation.z^2 + rotation.w^2
    assert(norm > 0.9 and norm < 1.1, "Headset rotation is not valid")
    return head, origin_read()
end
local function reset_head_position()
    local ok, head, origin = pcall(tracking_read)
    if not ok then report("Head-position reset unavailable: " .. tostring(head), true); return end
    local changed = pcall(function()
        vr.set_standing_origin(head)
        assert(same_vector(origin_read(), head), "Origin did not read back")
    end)
    if not changed then
        local restored = pcall(function()
            vr.set_standing_origin(origin)
            assert(same_vector(origin_read(), origin))
        end)
        report(restored and "Head-position reset failed; original restored."
            or "Head-position reset failed; restore incomplete.", true)
        return
    end
    origin_undo, origin_written = origin, head
    report("Current head position is now the camera origin. Rotation and game camera are unchanged.")
end
local function undo_head_position()
    local ok = pcall(function()
        assert(same_vector(origin_read(), origin_written), "Origin changed elsewhere")
        vr.set_standing_origin(origin_undo)
        assert(same_vector(origin_read(), origin_undo), "Origin restore did not read back")
    end)
    if ok then origin_undo, origin_written = nil, nil end
    report(ok and "Previous head-position origin restored."
        or "Origin changed elsewhere or restore failed; reset not undone.", not ok)
end

local function draw_group(name)
    for _, spec in ipairs(groups[name]) do
        local key = spec[1]
        local current = read(key)
        if current == nil then imgui.text(spec[2] .. ": unavailable in this backend")
        else
            local changed, value
            if spec[3] == nil then changed, value = imgui.checkbox(spec[2], current)
            else changed, value = imgui.drag_float(spec[2], current, spec[5], spec[3], spec[4], "%.3f") end
            if changed then change(key, value) end
        end
    end
    if imgui.button("Undo " .. name .. " edits") then restore_group(name) end
end

uevr.sdk.callbacks.on_draw_ui(function()
    if menu_owned then
        menu_wait_clear = true
        menu_release("Manual controls opened; reopen the game menu to resume / 已打开手动控制，请重新打开游戏菜单")
    end
    imgui.text("WuWa VR comfort controls")
    imgui.text("Changes apply live. UEVR saves current settings on exit.")
    draw_group("rendering")
    imgui.text("These are live switches. Replacing a backend DLL still needs a restart.")
    imgui.text("Keep Native Stereo Fix on for materials. The supplied timing fix uses r.OneFrameThreadLag=0.")
    draw_group("input")
    imgui.text("For a physical gamepad, turn motion input off. Head tracking stays on.")
    draw_group("visibility")
    imgui.text("Hiding this layer also hides game menus. Show it here before opening Esc/map.")
    if imgui.button("Show game UI") then change("VR_EnableGUI", true) end
    if imgui.button("Hide game UI") then change("VR_EnableGUI", false) end
    if read("VR_ToggleSlateGUIKey") == 119 then
        imgui.text("F8: show/hide game UI while the game has keyboard focus.")
    elseif imgui.button("Use F8 to show/hide game UI") then
        change("VR_ToggleSlateGUIKey", 119) -- VK_F8, handled by UEVR's existing key binding
    end
    if undo.VR_ToggleSlateGUIKey ~= nil and imgui.button("Undo UI shortcut change") then
        restore_group("shortcuts")
    end
    local changed, value = imgui.checkbox("Show camera controls", show_camera)
    if changed then show_camera = value end
    if show_camera then
        imgui.text("Pitch = looking up/down. Camera offsets use game units.")
        imgui.text("World scale also multiplies tracked head movement; 10x can displace the camera far away.")
        if imgui.button("Restore earlier camera (0.875x)") then earlier_camera() end
        if imgui.button("Reset head position at current pose") then reset_head_position() end
        if origin_undo ~= nil and imgui.button("Undo head-position reset") then undo_head_position() end
        local ok, head, origin = pcall(tracking_read)
        if ok then
            imgui.text(string.format("Tracked offset from origin (m): %.3f, %.3f, %.3f",
                head.x - origin.x, head.y - origin.y, head.z - origin.z))
        else imgui.text("Tracking offset unavailable in this session.") end
        draw_group("camera")
    end
    changed, value = imgui.checkbox("Show HUD controls", show_hud)
    if changed then show_hud = value end
    if show_hud then
        if imgui.button("Compact centered HUD") then centered_preset("Compact", 1.15, 2.5) end
        if imgui.button("Larger centered menu") then centered_preset("Menu reading", 1.6, 2.5) end
        draw_group("hud")
        local height, distance = read("UI_Size"), read("UI_Distance")
        if height ~= nil and distance ~= nil then
            imgui.text(string.format("Panel height spans %.0f degrees when centered.",
                math.deg(2 * math.atan(height / (2 * distance)))))
        end
        imgui.text("Move/resize the panel, then save a layout. Switching is manual.")
        imgui.text("Menu/dialogue layouts change placement; they do not repair missing content.")
        for _, name in ipairs(layout_names) do
            if imgui.button("Save " .. name .. " HUD") then save_layout(name) end
            if imgui.button("Use " .. name .. " HUD") then use_layout(name) end
        end
        imgui.text("Temporary menu comfort (opt-in, this Lua session) / 临时菜单舒适设置（仅本次运行）")
        changed, value = imgui.checkbox("Use saved Menu HUD temporarily / 临时使用已保存的菜单 HUD", menu_layout_enabled)
        if changed then menu_layout_enabled = value; menu_wait_clear = true; menu_clear_time = 0 end
        changed, value = imgui.checkbox("Temporarily show hidden UI in game menus / 游戏菜单中临时显示隐藏的界面", menu_visibility_enabled)
        if changed then menu_visibility_enabled = value; menu_wait_clear = true; menu_clear_time = 0 end
        imgui.text("Close UEVR, then reopen the game menu. Saved Menu placement is optional; no preset is guessed.")
        imgui.text("关闭 UEVR 后重新打开游戏菜单。菜单位置使用已保存的布局，不会自动创建预设。")
        imgui.text("Restores owned HUD values on exit. Manual edits take priority. No camera, portal or rendering changes.")
        imgui.text("退出后恢复仍由此功能控制的 HUD 设置，保留手动修改。不改变相机、空间窗口或渲染。")
        imgui.text("Native-menu detection can miss menus; this does not detect cutscenes or guarantee subtitles.")
        imgui.text("游戏菜单检测可能遗漏；此功能不检测过场，也不能保证字幕显示。")
        imgui.text("Menu comfort / 菜单舒适设置: " .. menu_status)
    end
    if imgui.button("Reset HUD aspect / 重置 HUD 比例") then hud_refresh:request() end
    imgui.text(hud_refresh.status)
    imgui.text("Refreshes the current HUD without opening Esc or changing its size/distance. / 不打开 Esc，不改变 HUD 大小或距离。")
    if imgui.button("Recenter view and HUD") then
        local ok = pcall(vr.recenter_view)
        report(ok and "View recentered." or "Recenter unavailable.", not ok)
    end
    if imgui.button("Level horizon") then
        local ok = pcall(vr.recenter_horizon)
        report(ok and "Horizon leveled." or "Horizon control unavailable.", not ok)
    end
    imgui.text("Last action: " .. message)
end)

uevr.sdk.callbacks.on_script_reset(function()
    auto_cinema:reset()
    hud_refresh:reset()
    last_mono_theatre = nil
    menu_layout_enabled, menu_visibility_enabled, menu_wait_clear = false, false, true
    menu_release("Script reset; previous HUD restored / 脚本重置，已尝试恢复之前的 HUD")
    -- Never strand the user with all game menus hidden after unloading this
    -- script. Restore only visibility still owned by this panel.
    if last_written.VR_EnableGUI == false and read("VR_EnableGUI") == false then
        write("VR_EnableGUI", undo.VR_EnableGUI)
    end
end)

log.log_info("WuWaComfort: Live camera/HUD controls loaded; settings preserved")
