-- Optional read-only detection. Native owns the expiring display lease; this
-- module never writes MonoTheatreMode, 2DScreenMode, portal or world scale.
local M = {}
local MOVIE = "MediaPlayer /Game/Aki/UI/UIResources/UiPlot/VideoPlayer/CommonVideoPlayer.CommonVideoPlayer"
local STORY = "Class /Script/LGUI.UIExtendToggleSpriteTransition"
local function finite(v) return type(v) == "number" and v == v and math.abs(v) < math.huge end
local function kind(p) return p:get_class():get_fname():to_string() end
local function bool_getter(o, name)
    local fn = o:get_class():find_function(name)
    if not fn then return nil end
    local field = fn:get_child_properties()
    local p = fn:find_property("ReturnValue")
    if not field or field:get_fname():to_string() ~= "ReturnValue" or kind(field) ~= "BoolProperty"
        or field:get_next() or not p or kind(p) ~= "BoolProperty" or not p:is_param()
        or not p:is_return_param() or not p:is_out_param() or p:is_reference_param() then return nil end
    local result = fn(o)
    if type(result) == "boolean" then return result end
    return nil
end

function M.new(env)
    local s = {status = "Automatic mono off", token = nil, last_poll = nil, classes = nil}
    local api, hook = env.api, env.types and env.types.UObjectHook
    local function valid(o) return o ~= nil and api:to_uobject(o:get_address()) ~= nil end
    local function property(o, desc, name, wanted)
        local p = desc:find_property(name)
        if not p or kind(p) ~= wanted then return nil end
        return o[name]
    end
    local function world()
        local engine = api:get_engine()
        if not valid(engine) then return nil end
        local viewport = property(engine, engine:get_class(), "GameViewport", "ObjectProperty")
        if not valid(viewport) then return nil end
        local value = property(viewport, viewport:get_class(), "World", "ObjectProperty")
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
    local function objects(class, cap, expected)
        if not valid(class) or class:get_full_name() ~= expected or not hook or not hook.get_objects_by_class then return nil end
        local values = hook.get_objects_by_class(class, false)
        if type(values) ~= "table" or #values > cap then return nil end
        return values
    end
    local function movie_signal()
        local list = objects(s.classes.movie, 16, "Class /Script/MediaAssets.MediaPlayer")
        if not list then return "unknown", "movie schema/index unavailable" end
        local target
        for _, o in ipairs(list) do
            if valid(o) and o:get_class():get_full_name() == "Class /Script/MediaAssets.MediaPlayer"
                and o:get_full_name() == MOVIE then
                if target then return "unknown", "ambiguous movie asset" end
                target = o
            end
        end
        if not target then return "unknown", "movie asset not loaded" end
        local playing = bool_getter(target, "IsPlaying")
        if playing == true then return "active", "CommonVideoPlayer playing" end
        if playing == nil then return "unknown", "movie IsPlaying signature unavailable" end
        local paused, preparing = bool_getter(target, "IsPaused"), bool_getter(target, "IsPreparing")
        if paused == true or preparing == true then return "hold", "movie paused/preparing" end
        if paused == nil or preparing == nil then return "unknown", "movie stop signature unavailable" end
        return "clear", "movie stopped"
    end
    local function story_signal(current)
        local list = objects(s.classes.story, 64, STORY)
        if not list then return "unknown", "dialogue class/index unavailable" end
        local unknown, matched = false, false
        for _, o in ipairs(list) do
            if valid(o) and o:get_class():get_full_name() == STORY and in_world(o, current) then
                -- StructObject:get_struct is the exposed reflection descriptor;
                -- validate each nested field before reading it. No raw offsets.
                local transition = property(o, o:get_class(), "TransitionState", "StructProperty")
                local hover = transition and property(transition, transition:get_struct(), "CheckedHoverState", "StructProperty")
                local sprite = hover and property(hover, hover:get_struct(), "Sprite", "ObjectProperty")
                if not transition or not hover or not valid(sprite) then unknown = true
                elseif sprite:get_fname():to_string() == "SP_PlotSkipBgIcon1" then
                    matched = true
                    local active = bool_getter(o, "GetIsActiveAndEnable")
                    if active == true then return "active", "active plot-skip widget" end
                    if active == nil then unknown = true end
                end
            end
        end
        if unknown then return "unknown", "dialogue schema unavailable" end
        return "clear", matched and "plot-skip widget inactive" or "no active plot-skip widget"
    end
    local function collect()
        local current = world()
        if not current then return {world = "0", active = 0, hold = 0, known = 0, detail = "Current world unavailable"} end
        local start_world = current:get_address()
        local movie_ok, movie, movie_detail = pcall(movie_signal)
        local story_ok, story, story_detail = pcall(story_signal, current)
        if not movie_ok then movie, movie_detail = "unknown", "movie reflection unavailable" end
        if not story_ok then story, story_detail = "unknown", "dialogue reflection unavailable" end
        local again = world()
        if not again or again:get_address() ~= start_world then
            return {world = "0", active = 0, hold = 0, known = 0, detail = "World changed during detection"}
        end
        local sample = {world = string.format("%.0f", start_world), active = 0, hold = 0, known = 0}
        for i, state in ipairs({movie, story}) do
            local bit = i == 1 and 1 or 2
            if state ~= "unknown" then sample.known = sample.known + bit end
            if state == "active" then sample.active = sample.active + bit end
            if state == "hold" then sample.hold = sample.hold + bit end
        end
        sample.detail = movie_detail .. "; " .. story_detail
        return sample
    end
    local function report(status, now)
        s.status = status
        local signature = status:gsub(" %[%s*detection %d+ ms%]", "")
        if signature ~= s.logged and (not s.log_at or now - s.log_at >= 2) then
            s.logged, s.log_at = signature, now
            if env.log then env.log("WuWaCinema: " .. status) end
        end
    end
    function s:reset(reason)
        if self.token then
            if reason then pcall(function()
                env.set("WuWaControls_AutoCinemaSample", env.encode({version = 1, generation = self.token,
                    world = "0", active = 0, hold = 0, known = 0, detail = reason}))
            end) end
            pcall(env.set, "WuWaControls_AutoCinemaProducer", "stop:" .. self.token)
        end
        self.token, self.classes, self.last_poll, self.class_attempts, self.class_retry = nil, nil, nil, nil, nil
    end
    local function tick()
        if env.get("VR_AutoCinema") ~= "true" then s:reset(); s.status = "Automatic cinema off"; return end
        if s.faulted then return end
        local now = tonumber(env.get("WuWaControls_Clock"))
        if not finite(now) or now < 0 then s:reset(); s.status = "Automatic mono clock unavailable"; return end
        if s.last_poll and now < s.last_poll then s:reset() end
        if s.last_poll and now - s.last_poll < 0.1 - 1e-6 then return end
        s.last_poll = now
        if env.get("WuWaControls_AutoCinemaSample") ~= "lease-v1" then report("Automatic mono needs a compatible backend", now); return end
        if s.token and env.get("WuWaControls_AutoCinemaProducer") ~= s.token then
            -- A newer script or native reset owns the producer now. A live
            -- old script must not repeatedly steal the lease back.
            s.token, s.classes, s.superseded = nil, nil, true
        end
        if s.superseded then report("Automatic mono detector reset; toggle automatic mode off and on", now); return end
        if not s.token then
            env.set("WuWaControls_AutoCinemaProducer", "start")
            s.token = env.get("WuWaControls_AutoCinemaProducer")
            if not tonumber(s.token) or tonumber(s.token) <= 0 then error("producer unavailable") end
            s.classes, s.class_attempts, s.class_retry = {}, {}, {}
        end
        -- Two exact class descriptors, never an asset/global-instance scan.
        -- Lazy engine classes may load later: at most three attempts each per
        -- enable, five seconds apart; successful descriptors use the index.
        for name, path in pairs({movie = "Class /Script/MediaAssets.MediaPlayer", story = STORY}) do
            if not valid(s.classes[name]) and (s.class_attempts[name] or 0) < 3 and now >= (s.class_retry[name] or 0) then
                s.class_attempts[name], s.class_retry[name] = (s.class_attempts[name] or 0) + 1, now + 5
                s.classes[name] = api:find_uobject(path)
            end
        end
        local sample = collect()
        local finished = tonumber(env.get("WuWaControls_Clock"))
        if finite(finished) then
            s.last_detection_ms = math.max(0, (finished - now) * 1000)
            -- Back off rather than turn an unexpectedly expensive class index
            -- or reflection path into recurring loading stalls. Native expiry
            -- restores the base view; re-enable explicitly after inspection.
            if s.last_detection_ms > 50 then
                s:reset("Detector suspended: exceeded 50 ms"); s.faulted = true
                report("Automatic cinema suspended: detection exceeded 50 ms", finished)
                return
            end
        end
        sample.version, sample.generation = 1, s.token
        sample.elapsed_ms = s.last_detection_ms or 0
        env.set("WuWaControls_AutoCinemaSample", env.encode(sample))
        report(env.get("WuWaControls_AutoCinemaStatus") .. "; detector: " .. sample.detail, now)
    end
    function s:tick()
        local ok = pcall(tick)
        if not ok then
            self:reset("Detector reflection/bridge unavailable"); self.faulted = true
            self.status = "Automatic cinema reflection/bridge unavailable; previous view restored"
            if env.log then pcall(env.log, "WuWaCinema: " .. self.status) end
        end
        local read_ok, enabled = pcall(env.get, "VR_AutoCinema")
        if read_ok and enabled ~= "true" then self.superseded, self.faulted = nil, nil end
    end
    return s
end
return M
