-- Scene info for recordings (1.2.6, 21 Oct 2026). While a recording runs and the Record page's
-- "Scene info" (WuWaRecording_SceneInfo) is on, this reads the text the game is showing (quest
-- tracker, dialogue speaker and line, banners, open menus), the map and the playable character,
-- and hands what changed to the recording's motion rows. Edits and summaries then start from what
-- the game said instead of a guess. Read-only: nothing in the game or its UI is changed.
-- Probed on 3.7 (21 Oct): every UIText is an LGUI UITextActor in the persistent level; the quest
-- tracker is PnlProcess/PnlInfo/PnlTitle/TxtTitle (quest) and PnlParent/PnlSubTitle/TxtParent (step).
-- Never read: the UID widget (UiView_UID_Prefab/UID), the ping, the HP bar and the watermark under
-- Map (PnlPosition: position, date and time). Runs of 7+ digits are masked in anything that is read.
local M = {}
local TEXT = "Class /Script/LGUI.UIText"
local SCAN_STEP, LIST_SECONDS, FULL_SECONDS = 64, 4, 30
local MAX_TEXTS, MAX_LENGTH, DEPTH, REPEAT = 48, 240, 12, 3
local SKIP = {UiView_UID_Prefab = true, UID = true, UiView_Ping = true, PnlPosition = true, FightRoleHp = true}

local function kind(p) return p:get_class():get_fname():to_string() end
local function cut(s)
    if type(s) ~= "string" then return nil end
    local stop = s:find("\0", 1, true)
    return stop and s:sub(1, stop - 1) or s
end
-- Rich-text tags go, whitespace collapses, 7+ digit runs are masked; text with no words is dropped
-- (numbers, distances like "518m", timers).
function M.clean(text)
    text = cut(text)
    if not text then return nil end
    text = text:gsub("<[^>]*>", ""):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "")
    text = text:gsub("%d%d%d%d%d%d%d+", function(d) return string.rep("#", #d) end)
    local words = text:gsub("[%d%s%p]", "")
    if words == "" or words == "m" or words == "ms" or words == "s" or words == "km" then return nil end
    if #text > MAX_LENGTH then text = text:sub(1, MAX_LENGTH) end
    return text
end

function M.new(env)
    local api, hook = env.api, env.types and env.types.UObjectHook
    local s = {status = "Scene info: idle", class = nil, list = nil, listed_at = nil, index = 0,
        meta = {}, found = {}, shown = {}, sent = {}, map = nil, pawn = nil, sent_map = nil, sent_pawn = nil,
        full_at = nil}
    local function valid(o) return o ~= nil and api:to_uobject(o:get_address()) ~= nil end
    local function property(o, name, wanted)
        local p = o:get_class():find_property(name)
        if not p or kind(p) ~= wanted then return nil end
        return o[name]
    end
    local function name_of(o) return cut(property(o, "displayName", "StrProperty")) end
    -- LGUI's own rule for "shown": the item and all its parents active. Signature checked first.
    local function active(o)
        local fn = o:get_class():find_function("IsUIActiveInHierarchy")
        local ret = fn and fn:get_child_properties()
        if not ret or ret:get_fname():to_string() ~= "ReturnValue" or kind(ret) ~= "BoolProperty" or ret:get_next() then
            return property(o, "bIsUIActive", "BoolProperty") == true
        end
        return o:IsUIActiveInHierarchy() == true
    end
    -- Key: the last three names of the item's path (PnlInfo/PnlTitle/TxtTitle); skip: anything under
    -- the UID, ping, HP or watermark panels. Worked out once per widget.
    local function describe(o)
        local names, skip = {}, false
        local own = name_of(o) or "?"
        if SKIP[own] then skip = true end
        local parent = property(o, "AttachParent", "ObjectProperty")
        for depth = 1, DEPTH do
            if not valid(parent) then break end
            local n = name_of(parent) or "?"
            if SKIP[n] then skip = true end
            if depth <= 2 then table.insert(names, 1, n) end
            parent = property(parent, "AttachParent", "ObjectProperty")
        end
        names[#names + 1] = own
        return {key = table.concat(names, "/"), skip = skip, own = own}
    end
    local function world()
        local engine = api:get_engine()
        local viewport = engine and property(engine, "GameViewport", "ObjectProperty")
        local value = viewport and property(viewport, "World", "ObjectProperty")
        return valid(value) and value or nil
    end
    local function context()
        local w = world()
        s.map = w and w:get_fname():to_string() or nil
        local pawn = api:get_local_pawn(0)
        s.pawn = valid(pawn) and pawn:get_class():get_fname():to_string() or nil
    end
    -- One bounded slice of the text widgets per call; a finished pass becomes the shown set.
    function s:tick(now)
        now = tonumber(now) or 0
        if not s.list or now - (s.listed_at or -1e9) >= LIST_SECONDS and s.index == 0 then
            if not valid(s.class) then s.class = api:find_uobject(TEXT) end
            if not valid(s.class) or s.class:get_full_name() ~= TEXT or not hook or not hook.get_objects_by_class then
                s.status = "Scene info: text class unavailable"; return
            end
            local list = hook.get_objects_by_class(s.class, false)
            if type(list) ~= "table" or #list > 20000 then s.status = "Scene info: text list unavailable"; return end
            -- A widget gone from the list may have its address reused by a new one: describe that one afresh.
            local meta = {}
            for _, o in ipairs(list) do
                local address = o:get_address()
                meta[address] = s.meta[address]
            end
            s.list, s.listed_at, s.index, s.meta = list, now, 0, meta
            context()
        end
        local last = math.min(#s.list, s.index + SCAN_STEP)
        for i = s.index + 1, last do
            local o = s.list[i]
            if valid(o) then
                local address = o:get_address()
                local m = s.meta[address]
                -- A different name at a known address is a new widget there: describe it again.
                if not m or m.own ~= (name_of(o) or "?") then m = describe(o); s.meta[address] = m end
                if not m.skip and active(o) then
                    local alpha = property(o, "Alpha", "FloatProperty")
                    local text = (alpha == nil or alpha > 0.01) and M.clean(property(o, "text", "StrProperty")) or nil
                    if text then
                        local key, n = m.key, 2
                        while s.found[key] and s.found[key] ~= text do key = m.key .. "#" .. n; n = n + 1 end
                        s.found[key] = text
                    end
                end
            end
        end
        s.index = last
        if last >= #s.list then
            local shown, count = {}, 0
            for key, text in pairs(s.found) do
                if count < MAX_TEXTS then shown[key] = text; count = count + 1 end
            end
            s.shown, s.found, s.index = shown, {}, 0
            s.status = string.format("Scene info: %d texts on %s", count, s.map or "?")
        end
    end
    local function copy(t) local out = {}; for k, v in pairs(t) do out[k] = v end; return out end
    -- The scene part for this recording row: what changed ({set = {key = text}, gone = {keys}, map, pawn}),
    -- or every FULL_SECONDS everything shown ({full = true, texts = ...}) so a reader can start anywhere.
    -- The native recorder drops a row that comes within 33 ms of the last one and nothing reports that, so
    -- each payload rides on REPEAT rows in a row; applying one twice changes nothing. nil: nothing to send.
    function s:take(now)
        now = tonumber(now) or 0
        if not s.full_at or now - s.full_at >= FULL_SECONDS then
            s.full_at = now
            s.sent, s.sent_map, s.sent_pawn = copy(s.shown), s.map, s.pawn
            s.pending, s.repeats = {full = true, texts = copy(s.shown), map = s.map, pawn = s.pawn}, REPEAT
        else
            local set, gone, any = {}, {}, false
            for key, text in pairs(s.shown) do
                if s.sent[key] ~= text then set[key] = text; any = true end
            end
            for key in pairs(s.sent) do
                if s.shown[key] == nil then gone[key] = true; any = true end
            end
            local map, pawn = s.map ~= s.sent_map, s.pawn ~= s.sent_pawn
            if any or map or pawn then
                s.sent, s.sent_map, s.sent_pawn = copy(s.shown), s.map, s.pawn
                local p = s.pending or {}
                if p.full then
                    for key, text in pairs(set) do p.texts[key] = text end
                    for key in pairs(gone) do p.texts[key] = nil end
                else
                    p.set = p.set or {}
                    local was = {}
                    for _, key in ipairs(p.gone or {}) do was[key] = true end
                    for key, text in pairs(set) do p.set[key] = text; was[key] = nil end
                    for key in pairs(gone) do p.set[key] = nil; was[key] = true end
                    local list = {}
                    for key in pairs(was) do list[#list + 1] = key end
                    table.sort(list)
                    p.gone = #list > 0 and list or nil
                    if not next(p.set) then p.set = nil end
                end
                if map then p.map = s.map end
                if pawn then p.pawn = s.pawn end
                s.pending, s.repeats = p, REPEAT
            end
        end
        if (s.repeats or 0) > 0 then
            s.repeats = s.repeats - 1
            return s.pending
        end
        s.pending = nil
        return nil
    end
    -- A new recording starts from a full snapshot; widget descriptions are kept (they do not change).
    function s:reset()
        s.list, s.index, s.found, s.shown, s.sent, s.full_at = nil, 0, {}, {}, {}, nil
        s.sent_map, s.sent_pawn, s.pending, s.repeats = nil, nil, nil, 0
        s.status = "Scene info: idle"
    end
    return s
end
return M
