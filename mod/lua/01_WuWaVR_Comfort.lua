-- Live layout controls for the existing UEVR camera and extracted LGUI quad.
-- No gameplay button remapping, Unreal object lookup, or render-path changes.
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
        {"VR_NativeStereoFix", "Native Stereo Fix (experimental on SteamVR)"},
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
    imgui.text("WuWa VR comfort controls")
    imgui.text("Changes apply live. UEVR saves current settings on exit.")
    draw_group("rendering")
    imgui.text("These are live switches. Replacing a backend DLL still needs a restart.")
    imgui.text("NSF off avoids the observed SteamVR stutter; recheck eyes, shadows and menus.")
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
    end
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
    -- Never strand the user with all game menus hidden after unloading this
    -- script. Restore only visibility still owned by this panel.
    if last_written.VR_EnableGUI == false and read("VR_EnableGUI") == false then
        write("VR_EnableGUI", undo.VR_EnableGUI)
    end
end)

log.log_info("WuWaComfort: Live camera/HUD controls loaded; settings preserved")
