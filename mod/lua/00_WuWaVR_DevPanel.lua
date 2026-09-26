-- WuWa VR live development controls.
-- Load this before the feature scripts so they share WUWA_VR_DEV_STATE.

local callbacks = uevr.sdk.callbacks
local log_functions = uevr.params.functions

local defaults = {
    version = 1,
    ui_fix_enabled = false,
    freecam_bindings_enabled = false,
    profiler_enabled = true,
    show_diagnostics = false,
    show_capture_tools = false,
    screen_capture_request = 0,
    screen_capture_label = "none",
    capture_duration = 20.0,
    target_refresh_hz = 90.0,
    capture_active = false,
    capture_elapsed = 0.0,
    capture_frames = 0,
    live_elapsed = 0.0,
    live_frames = 0,
    live_fps = 0.0,
    live_frame_ms = 0.0,
    last_capture_fps = 0.0,
    last_capture_frame_ms = 0.0,
    last_event = "Panel loaded; UI fix starts disabled",
    last_error = "none",
    marker = 0,
    xinput_buttons = 0,
    xinput_left_x = 0,
    xinput_left_y = 0,
    xinput_right_x = 0,
    xinput_right_y = 0,
    xinput_left_trigger = 0,
    xinput_right_trigger = 0,
}

local state = rawget(_G, "WUWA_VR_DEV_STATE") or {}
for key, value in pairs(defaults) do
    if state[key] == nil then state[key] = value end
end
rawset(_G, "WUWA_VR_DEV_STATE", state)

-- The community UI fix remained left-eye-only in Native Stereo and caused
-- controller clamping plus map/menu softlocks. Keep the shared field for
-- compatibility with the reference script, but never expose an activation
-- control in the live profile.
state.ui_fix_enabled = false

local function log_event(message)
    state.last_event = message
    log_functions.log_info("WuWaVRDev: " .. message)
end

local function report_error(source, message)
    state.last_error = source .. ": " .. tostring(message)
    log_functions.log_error("WuWaVRDev: " .. state.last_error)
end

state.log_event = log_event
state.report_error = report_error

local function start_capture()
    state.capture_active = true
    state.capture_elapsed = 0.0
    state.capture_frames = 0
    log_event(string.format("Started %.1fs FPS capture (UI=%s, FreecamBindings=%s)",
        state.capture_duration,
        tostring(state.ui_fix_enabled),
        tostring(state.freecam_bindings_enabled)))
end

local function half_rate_note(fps)
    local half_rate = state.target_refresh_hz / 2.0
    local tolerance = math.max(1.0, half_rate * 0.05)
    if math.abs(fps - half_rate) <= tolerance then
        return string.format("matches half of %.0f Hz; compositor reprojection is likely active", state.target_refresh_hz)
    end
    return "no half-refresh signature detected"
end

callbacks.on_pre_engine_tick(function(engine, delta)
    if not state.profiler_enabled or delta <= 0.0 then
        return
    end

    state.live_elapsed = state.live_elapsed + delta
    state.live_frames = state.live_frames + 1

    if state.live_elapsed >= 0.5 then
        state.live_fps = state.live_frames / state.live_elapsed
        state.live_frame_ms = 1000.0 / math.max(state.live_fps, 0.001)
        state.live_elapsed = 0.0
        state.live_frames = 0
    end

    if state.capture_active then
        state.capture_elapsed = state.capture_elapsed + delta
        state.capture_frames = state.capture_frames + 1

        if state.capture_elapsed >= state.capture_duration then
            state.last_capture_fps = state.capture_frames / state.capture_elapsed
            state.last_capture_frame_ms = 1000.0 / math.max(state.last_capture_fps, 0.001)
            state.capture_active = false
            log_event(string.format("Capture complete: %.2f FPS / %.2f ms (UI=%s, FreecamBindings=%s); %s",
                state.last_capture_fps,
                state.last_capture_frame_ms,
                tostring(state.ui_fix_enabled),
                tostring(state.freecam_bindings_enabled),
                half_rate_note(state.last_capture_fps)))
        end
    end
end)

-- Read-only telemetry. This callback deliberately does not alter the gamepad state.
callbacks.on_xinput_get_state(function(retval, user_index, input_state)
    if input_state == nil or input_state.Gamepad == nil then
        return
    end

    local gamepad = input_state.Gamepad
    state.xinput_buttons = gamepad.wButtons or 0
    state.xinput_left_x = gamepad.sThumbLX or 0
    state.xinput_left_y = gamepad.sThumbLY or 0
    state.xinput_right_x = gamepad.sThumbRX or 0
    state.xinput_right_y = gamepad.sThumbRY or 0
    state.xinput_left_trigger = gamepad.bLeftTrigger or 0
    state.xinput_right_trigger = gamepad.bRightTrigger or 0
end)

callbacks.on_draw_ui(function()
    imgui.text("WuWa VR utilities")
    imgui.text("Optional freecam shortcuts start off while gamepad reliability is being checked.")

    local changed_freecam, new_freecam = imgui.checkbox("Enable freecam bindings", state.freecam_bindings_enabled)
    if changed_freecam then
        state.freecam_bindings_enabled = new_freecam
        if not new_freecam and state.disable_freecam ~= nil then
            local ok, err = pcall(state.disable_freecam)
            if not ok then
                report_error("freecam cleanup", err)
            end
        end
        log_event("Freecam bindings " .. (new_freecam and "enabled" or "disabled"))
    end

    local changed_capture, new_capture = imgui.checkbox("Save screen evidence", state.show_capture_tools)
    if changed_capture then state.show_capture_tools = new_capture end
    if state.show_capture_tools then
        imgui.text("Requires the launcher's recording session. Choose the screen, then close UEVR.")
        imgui.text("Keep it visible for a few seconds. Review saved scene/UI images in the launcher.")
        for _, label in ipairs({"gameplay", "main-menu", "map", "dialogue", "issue"}) do
            if imgui.button("Capture next screen: " .. label) then
                state.screen_capture_request = state.screen_capture_request + 1
                state.screen_capture_label = label
                log_event(string.format("[WuWaCaptureRequest] label=%s sequence=%d",
                    label, state.screen_capture_request))
            end
        end
        imgui.text("Last requested screen: " .. state.screen_capture_label .. " (request only; check launcher for result)")
    end

    local changed_diagnostics, new_diagnostics = imgui.checkbox("Show development diagnostics", state.show_diagnostics)
    if changed_diagnostics then state.show_diagnostics = new_diagnostics end
    if not state.show_diagnostics then return end
    imgui.text(string.format("Engine: %.1f FPS / %.2f ms", state.live_fps, state.live_frame_ms))
    imgui.text("Legacy UI fix: quarantined (left-eye-only; input/menu regressions)")

    local changed_profiler, new_profiler = imgui.checkbox("Enable lightweight profiler", state.profiler_enabled)
    if changed_profiler then
        state.profiler_enabled = new_profiler
        log_event("Profiler " .. (new_profiler and "enabled" or "disabled"))
    end

    local changed_duration, new_duration = imgui.drag_float("Capture seconds", state.capture_duration, 1.0, 5.0, 120.0, "%.0f")
    if changed_duration then
        state.capture_duration = new_duration
    end

    local changed_refresh, new_refresh = imgui.drag_float("Headset refresh Hz", state.target_refresh_hz, 1.0, 60.0, 144.0, "%.0f")
    if changed_refresh then
        state.target_refresh_hz = new_refresh
    end

    if not state.capture_active then
        if imgui.button("Start FPS capture") then
            start_capture()
        end
    else
        imgui.text(string.format("Capturing... %.1f / %.1f seconds", state.capture_elapsed, state.capture_duration))
    end

    if imgui.button("Add log marker") then
        state.marker = state.marker + 1
        log_event("Manual marker " .. tostring(state.marker))
    end

    imgui.text(string.format("Last capture: %.2f FPS / %.2f ms", state.last_capture_fps, state.last_capture_frame_ms))
    if state.last_capture_fps > 0.0 then
        imgui.text("Capture diagnosis: " .. half_rate_note(state.last_capture_fps))
    end
    imgui.text(string.format("Lua input buttons: 0x%04X  LT/RT: %d/%d",
        state.xinput_buttons, state.xinput_left_trigger, state.xinput_right_trigger))
    imgui.text(string.format("Lua input sticks: L %d,%d  R %d,%d",
        state.xinput_left_x, state.xinput_left_y, state.xinput_right_x, state.xinput_right_y))
    imgui.text("Last event: " .. state.last_event)
    imgui.text("Last error: " .. state.last_error)
end)

callbacks.on_script_reset(function()
    if state.disable_freecam ~= nil then
        pcall(state.disable_freecam)
    end
    log_functions.log_info("WuWaVRDev: Lua state reset cleanup requested")
end)

log_event("Development panel initialized")
