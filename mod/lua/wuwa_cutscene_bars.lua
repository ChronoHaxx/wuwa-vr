-- Full VR: hide the game's cutscene letterbox bars (test 21, 18 Oct 2026).
-- With Graphics > Cutscene Mode on letterbox, WuWa draws two black UITextures in the
-- view UiView_BlackFadeScreen_Prefab: "BlackTexture" (bottom) and "BlackTexture1" (top),
-- each 2560x180 on the 1440-high canvas, inheritAlpha off, so they show even while the
-- view's own alpha is 0. In full VR they only cover the world above and below the
-- subtitles. While full VR is on (not the stereo or mono screen) and the setting
-- WuWaStereo_HideCutsceneBars is on, this module sets their alpha to 0 through the
-- game's own SetAlpha, only while each is letterbox-sized (a quarter of the canvas or
-- less), and puts the game's alpha back when that ends. The screens keep the game's bars.
local M = {}
local TEXTURE = "Class /Script/LGUI.UITexture"
local VIEW = "UiView_BlackFadeScreen_Prefab"
local NAMES = {BlackTexture = true, BlackTexture1 = true}
local SCAN_STEP, RETRY_SECONDS, POLL_SECONDS = 200, 3, 0.2
local LETTERBOX = 0.25 -- largest bar height, as a share of the view's height, that counts as letterbox

local function finite(v) return type(v) == "number" and v == v and math.abs(v) < math.huge end
local function kind(p) return p:get_class():get_fname():to_string() end

function M.new(env)
    local api, hook = env.api, env.types and env.types.UObjectHook
    local s = {status = "Cutscene bars: off", bars = nil, scan = nil, retry_at = 0, last_poll = nil, class = nil}
    local function valid(o) return o ~= nil and api:to_uobject(o:get_address()) ~= nil end
    local function property(o, name, wanted)
        local p = o:get_class():find_property(name)
        if not p or kind(p) ~= wanted then return nil end
        return o[name]
    end
    -- UEVR returns an FString with its terminating NUL; the name ends at the first NUL.
    local function display_name(o)
        local name = property(o, "displayName", "StrProperty")
        if type(name) ~= "string" then return nil end
        local stop = name:find("\0", 1, true)
        return stop and name:sub(1, stop - 1) or name
    end
    -- SetAlpha(newAlpha: float) and nothing else, checked before every call.
    local function set_alpha(o, value)
        local fn = o:get_class():find_function("SetAlpha")
        local p = fn and fn:get_child_properties()
        if not p or p:get_fname():to_string() ~= "newAlpha" or kind(p) ~= "FloatProperty" or p:get_next() then return false end
        o:SetAlpha(value)
        return true
    end
    local function report(text)
        if text ~= s.status then
            s.status = text
            if env.log then env.log("WuWaBars: " .. text) end
        end
    end
    local function world()
        local engine = api:get_engine()
        local viewport = engine and property(engine, "GameViewport", "ObjectProperty")
        local value = viewport and property(viewport, "World", "ObjectProperty")
        return valid(value) and value or nil
    end
    local function in_world(o, current)
        for _ = 1, 16 do
            if not valid(o) then return false end
            if o:get_address() == current:get_address() then return true end
            o = o:get_outer()
        end
        return false
    end
    -- Put the game's alpha back on every bar we hid, unless the game has changed it since.
    local function restore(reason)
        for _, bar in ipairs(s.bars or {}) do
            if bar.hidden and valid(bar.object) then
                local alpha = property(bar.object, "Alpha", "FloatProperty")
                if finite(alpha) and alpha <= 0.001 then pcall(set_alpha, bar.object, bar.original) end
            end
            bar.hidden = false
        end
        if reason then report(reason) end
    end
    local function forget()
        restore()
        s.bars, s.scan = nil, nil
    end
    -- One bounded slice of the search per tick; the view persists, so this runs rarely.
    local function search(current, now)
        if not s.scan then
            if now < s.retry_at then return end
            if not valid(s.class) then s.class = api:find_uobject(TEXTURE) end
            if not valid(s.class) or s.class:get_full_name() ~= TEXTURE or not hook or not hook.get_objects_by_class then
                s.retry_at = now + RETRY_SECONDS; report("Cutscene bars: texture class unavailable"); return
            end
            local list = hook.get_objects_by_class(s.class, false)
            if type(list) ~= "table" or #list > 8000 then s.retry_at = now + RETRY_SECONDS; return end
            s.scan = {list = list, index = 0, found = {}}
        end
        local scan = s.scan
        local last = math.min(#scan.list, scan.index + SCAN_STEP)
        for i = scan.index + 1, last do
            local o = scan.list[i]
            if valid(o) and NAMES[display_name(o) or ""] and in_world(o, current) then
                local parent = property(o, "AttachParent", "ObjectProperty")
                if valid(parent) and display_name(parent) == VIEW then
                    scan.found[#scan.found + 1] = {object = o, view = parent, hidden = false, original = 1}
                end
            end
        end
        scan.index = last
        if last < #scan.list then return end
        s.scan = nil
        -- A closed view can linger until garbage collection: keep only the bars of an active view.
        local live = {}
        for _, bar in ipairs(scan.found) do
            if property(bar.view, "bIsUIActive", "BoolProperty") == true then live[#live + 1] = bar end
        end
        if #live == 0 or #live > 2 then
            s.retry_at = now + RETRY_SECONDS
            report(#live == 0 and "Cutscene bars: none found yet" or "Cutscene bars: ambiguous, left alone")
            return
        end
        s.bars = live
    end
    local function apply(current)
        local hidden = 0
        for _, bar in ipairs(s.bars) do
            local o = bar.object
            if not (valid(o) and in_world(o, current)) then forget(); return end
            local height = property(o, "Height", "FloatProperty")
            local view_height = property(bar.view, "Height", "FloatProperty")
            local alpha = property(o, "Alpha", "FloatProperty")
            if not finite(height) or not finite(view_height) or view_height <= 0 or not finite(alpha) then
                restore("Cutscene bars: layout unreadable, left alone"); s.bars = nil; s.retry_at = 0; return
            end
            local letterbox = height > 0 and height <= view_height * LETTERBOX
            if letterbox then
                if alpha > 0.001 then
                    -- The game showed it (again): remember its alpha, then hide.
                    bar.original = alpha
                    if set_alpha(o, 0.0) then bar.hidden = true end
                end
                if bar.hidden then hidden = hidden + 1 end
            elseif bar.hidden then
                -- Larger than a letterbox (a fade to black): give it back to the game.
                if alpha <= 0.001 then set_alpha(o, bar.original) end
                bar.hidden = false
            end
        end
        report(hidden > 0 and string.format("Cutscene bars: %d hidden in full VR", hidden) or "Cutscene bars: watching (none shown)")
    end
    local function tick()
        local now = tonumber(env.get("WuWaControls_Clock"))
        if not finite(now) then return end
        if s.last_poll and now >= s.last_poll and now - s.last_poll < POLL_SECONDS then return end
        s.last_poll = now
        local enabled = env.get("WuWaStereo_HideCutsceneBars") == "true"
        local screen = env.get("WuWaControls_EffectiveScreen") == "true" or env.get("WuWaControls_EffectiveMonoTheatre") == "true"
        if not enabled or screen then
            if s.bars then restore(enabled and "Cutscene bars: shown on the screen view" or "Cutscene bars: off") end
            if not enabled then s.status = "Cutscene bars: off" end
            return
        end
        local current = world()
        if not current then forget(); return end
        if not s.bars then search(current, now) end
        if s.bars then apply(current) end
    end
    function s:tick()
        local ok, err = pcall(tick)
        if not ok then
            pcall(restore)
            self.bars, self.scan, self.retry_at = nil, nil, (tonumber(env.get("WuWaControls_Clock")) or 0) + 10
            report("Cutscene bars: reflection failed, retrying (" .. tostring(err):sub(1, 80) .. ")")
        end
    end
    function s:reset() pcall(forget) end
    return s
end
return M
