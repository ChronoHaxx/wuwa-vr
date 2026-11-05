-- Hides the game's user ID at the source (1.2.7, 31 Oct 2026; owner: hide the UID in the mod before 3.8). The ID
-- is one LGUI text, UiView_UID_Prefab/UIC/UID ("User ID: <9 digits>", probed on 3.7 for wuwa_scene_log.lua). The
-- privacy boxes cover where it should be, but in every 3.7 story part a view switch or a dialogue band drawn a
-- frame early showed it outside them for 1-5 frames. With the text's alpha at 0 the game never draws it, in any
-- view, so nothing is left to cover. Found once in slices of the text list, checked every tick (the game may set
-- the alpha again), and the alpha it had comes back when hiding stops. Only the alpha changes: the widget stays
-- active, so the game's own UI logic sees no difference. The ESC profile screen's ID row is a different widget;
-- its privacy box stays.
local M = {}
local TEXT = "Class /Script/LGUI.UIText"
local SCAN_STEP, RELIST_SECONDS, DEPTH = 256, 2, 4

local function kind(p) return p:get_class():get_fname():to_string() end
local function cut(s)
    if type(s) ~= "string" then return nil end
    local stop = s:find("\0", 1, true)
    return stop and s:sub(1, stop - 1) or s
end

function M.new(env)
    local api, hook = env.api, env.types and env.types.UObjectHook
    local s = {status = "UID: not looked for yet", target = nil, address = nil, saved = nil, hidden = false,
        list = nil, index = 0, listed_at = nil, class = nil}
    local function valid(o) return o ~= nil and api:to_uobject(o:get_address()) ~= nil end
    local function property(o, name, wanted)
        local p = o:get_class():find_property(name)
        if not p or kind(p) ~= wanted then return nil end
        return o[name]
    end
    local function name_of(o) return cut(property(o, "displayName", "StrProperty")) end
    -- "UID" alone is too common a name: it must sit under the UID view.
    local function is_uid(o)
        if name_of(o) ~= "UID" then return false end
        local parent = property(o, "AttachParent", "ObjectProperty")
        for _ = 1, DEPTH do
            if not valid(parent) then return false end
            if name_of(parent) == "UiView_UID_Prefab" then return true end
            parent = property(parent, "AttachParent", "ObjectProperty")
        end
        return false
    end
    local function can_fade(o)
        local c = o:get_class()
        return c:find_function("SetAlpha") ~= nil and c:find_function("GetAlpha") ~= nil
    end
    local function still_there(o) return valid(o) and o:get_address() == s.address and name_of(o) == "UID" end
    local function hide(o)
        local alpha = tonumber(o:GetAlpha()) or 1
        if alpha > 0.001 then
            if not s.hidden or not s.saved then s.saved = alpha end
            o:SetAlpha(0.0)
        end
        s.hidden = true
        s.status = "UID: hidden"
    end

    -- want: hide now (the privacy setting isn't Off). now: seconds, for re-listing the texts.
    function s:tick(now, want)
        if not want then s:restore("privacy off"); return end
        if s.target and still_there(s.target) then hide(s.target); return end
        s.target, s.address, s.hidden, s.saved = nil, nil, false, nil
        s.status = "UID: looking"
        now = tonumber(now) or 0
        -- A pass that found nothing waits for the next listing (no UID view on the title screen, say).
        if not s.list then
            if s.listed_at and now - s.listed_at < RELIST_SECONDS then return end
            s.listed_at = now
            if not valid(s.class) then
                local found, class = pcall(function() return api:find_uobject(TEXT) end)
                s.class = found and class or nil
            end
            if not valid(s.class) or s.class:get_full_name() ~= TEXT or not hook or not hook.get_objects_by_class then
                s.status = "UID: text class unavailable"; return
            end
            local list = hook.get_objects_by_class(s.class, false)
            if type(list) ~= "table" then s.status = "UID: text list unavailable"; return end
            s.list, s.listed_at, s.index = list, now, 0
        end
        local last = math.min(#s.list, s.index + SCAN_STEP)
        for i = s.index + 1, last do
            local o = s.list[i]
            if valid(o) and is_uid(o) then
                s.list, s.index = nil, 0
                if not can_fade(o) then s.status = "UID: found, but it can't be faded"; return end
                s.target, s.address = o, o:get_address()
                hide(o)
                return
            end
        end
        if last >= #s.list then s.list, s.index = nil, 0 else s.index = last end
        s.status = "UID: looking"
    end

    -- Back to the alpha the game had; the found widget is kept so hiding again is immediate.
    function s:restore(why)
        if s.hidden and s.target and still_there(s.target) and s.saved then s.target:SetAlpha(s.saved) end
        s.hidden, s.saved = false, nil
        s.status = "UID: shown (" .. tostring(why) .. ")"
    end
    return s
end
return M
