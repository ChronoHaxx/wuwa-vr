-- Explicit HUD refresh and bounded 2D-transition observer. Loading is inert.
-- Calls reflected LGUI APIs on the game thread only; no offsets or setting writes.
local M = {}
local function finite(v) return type(v) == "number" and v == v and math.abs(v) < math.huge end
local function address(o) return o and o:get_address() end
local function kind(p) return p:get_class():get_fname():to_string() end

-- Validate the entire callable signature, including direction, before invoking it.
local function method(o, name, expected)
    local fn = o:get_class():find_function(name)
    if not fn then return nil end
    local p = fn:get_child_properties()
    for _, spec in ipairs(expected) do
        if not p or p:get_fname():to_string() ~= spec[1] or kind(p) ~= spec[2] then return nil end
        -- get_child_properties/get_next expose FField, not FProperty in Lua.
        -- Resolve this same ordered name on the function for the typed flags;
        -- both descriptors must agree on name/type. No pointer cast or offset.
        local property = fn:find_property(spec[1])
        if not property or property:get_fname():to_string() ~= spec[1] or kind(property) ~= spec[2]
            or not property:is_param() or property:is_return_param() ~= (spec[1] == "ReturnValue") then return nil end
        if spec[1] ~= "ReturnValue" and (property:is_out_param() ~= (spec[3] == "out")
            or (spec[3] ~= "out" and property:is_reference_param())) then return nil end
        p = p:get_next()
    end
    if p then return nil end
    if #expected == 0 and fn:get_properties_size() ~= 0 then return nil end
    return fn
end
local RETURNS = {
    bool = {{"ReturnValue", "BoolProperty"}}, object = {{"ReturnValue", "ObjectProperty"}},
    byte = {{"ReturnValue", "ByteProperty"}}, enum = {{"ReturnValue", "EnumProperty"}},
    float = {{"ReturnValue", "FloatProperty"}},
}
local SET_PROJECTION = {{"InProjectionType", "ByteProperty"}, {"InFovAngle", "FloatProperty"},
    {"InNearClipPlane", "FloatProperty"}, {"InFarClipPlane", "FloatProperty"}}

function M.new(env)
    local api = env.api
    local s = {status = "HUD aspect reset ready / HUD 比例重置就绪", pending = nil, last_mode = nil}
    local function valid(o)
        return o ~= nil and api:to_uobject(address(o)) ~= nil
    end
    local function prop(o, name, wanted)
        if not valid(o) then return nil end
        local p = o:get_class():find_property(name)
        if not p or kind(p) ~= wanted then return nil end
        return o[name]
    end
    local function in_world(o, world)
        for _ = 1, 16 do
            if not valid(o) then return false end
            if address(o) == address(world) then return true end
            o = o:get_outer()
        end
        return false
    end
    local function current_context()
        local engine = api:get_engine()
        local viewport = prop(engine, "GameViewport", "ObjectProperty")
        local world = prop(viewport, "World", "ObjectProperty")
        if not valid(world) then return nil end
        local pc = api:get_player_controller(0)
        local dims, dimensions_available
        if valid(pc) and in_world(pc, world) then
            local fn = method(pc, "GetViewportSize", {{"SizeX", "IntProperty", "out"}, {"SizeY", "IntProperty", "out"}})
            if fn then
                dimensions_available = true
                local x, y = {}, {}; fn(pc, x, y)
                if finite(x.result) and finite(y.result) and x.result >= 64 and y.result >= 64
                    and x.result <= 32768 and y.result <= 32768 then dims = {x.result, y.result} end
            end
        end
        return {world = world, dims = dims, dimensions_available = dimensions_available}
    end
    -- Each refusal names its reason (test 3g): the refresh found no HUD in
    -- every 3e/3f view switch, and the saved schema report never appeared.
    local function candidate(o, world, canvas_class)
        if not valid(o) then return nil, "invalid" end
        if not in_world(o, world) then return nil, "scaler outside viewport world" end
        local active = method(o, "GetIsActiveAndEnable", RETURNS.bool)
        if not active then return nil, "no GetIsActiveAndEnable" end
        if active(o) ~= true then return nil, "inactive" end
        -- Reference ForceUpdate dereferences Canvas before CheckCanvas: never
        -- invoke it on an uninitialized scaler, even when reflection resolves.
        local canvas = prop(o, "Canvas", "ObjectProperty")
        if not valid(canvas) then return nil, "no canvas" end
        if not canvas:is_a(canvas_class) then return nil, "canvas not LGUICanvas" end
        if not in_world(canvas, world) then return nil, "canvas outside viewport world" end
        local root = method(canvas, "GetRootCanvas", RETURNS.object)
        local mode = method(canvas, "GetRenderMode", RETURNS.enum)
        if not root then return nil, "no GetRootCanvas" end
        if not mode then return nil, "no GetRenderMode(enum)" end
        if address(root(canvas)) ~= address(canvas) then return nil, "not a root canvas" end
        local render_mode = mode(canvas)
        if render_mode ~= 0 then return nil, "render mode " .. tostring(render_mode) end
        local force = method(o, "ForceUpdate", {})
        local set = method(canvas, "SetProjectionParameters", SET_PROJECTION)
        local projection = method(o, "GetProjectionType", RETURNS.byte)
        local fov = method(o, "GetFovAngle", RETURNS.float)
        local near = method(o, "GetNearClipPlane", RETURNS.float)
        local far = method(o, "GetFarClipPlane", RETURNS.float)
        if not force then return nil, "no ForceUpdate()" end
        if not set then return nil, "SetProjectionParameters signature differs" end
        if not projection or not fov or not near or not far then return nil, "projection getters differ" end
        local values = {projection(o), fov(o), near(o), far(o)}
        if (values[1] ~= 0 and values[1] ~= 1) or not finite(values[2]) or values[2] <= 0 or values[2] >= 360
            or not finite(values[3]) or values[3] <= 0 or not finite(values[4]) or values[4] <= values[3] then
            return nil, string.format("projection values %s/%s/%s/%s", tostring(values[1]), tostring(values[2]), tostring(values[3]), tostring(values[4]))
        end
        return {object = o, canvas = canvas, force = force, set = set, values = values}
    end
    -- A setter with exactly one plain float parameter.
    local function float_setter(o, name)
        local fn = o:get_class():find_function(name)
        if not fn then return nil end
        local p = fn:get_child_properties()
        if not p or kind(p) ~= "FloatProperty" or p:get_next() then return nil end
        local property = fn:find_property(p:get_fname():to_string())
        if not property or not property:is_param() or property:is_out_param() or property:is_return_param() then return nil end
        return fn
    end
    -- Experiment (test 3i). WuWa's whole UI hangs off one LGUICanvasScaler (3h
    -- probe: one scaler, reference 2560 x 1440, match mode 2), which has no
    -- ForceUpdate. LGUI recomputes the layout when a scaler setting changes,
    -- so change MatchFromWidthToHeight by 0.001 now and restore it next tick.
    local function nudge(context, objects, scaler_class)
        local count = 0
        for _, o in ipairs(objects) do
            if valid(o) and o:is_a(scaler_class) and in_world(o, context.world) then
                local get = method(o, "GetMatchFromWidthToHeight", RETURNS.float)
                local set = float_setter(o, "SetMatchFromWidthToHeight")
                local value = get and get(o)
                if set and finite(value) and value >= 0 and value <= 1 then
                    set(o, value < 0.5 and value + 0.001 or value - 0.001)
                    s.restore = s.restore or {}
                    s.restore[#s.restore + 1] = {object = o, set = set, value = value}
                    count = count + 1
                end
            end
        end
        return count
    end
    local function refresh(context)
        local hook = env.types and env.types.UObjectHook
        if not hook or not hook.get_objects_by_class then return false, "Object index unavailable" end
        local scaler_class = api:find_uobject("Class /Script/LGUI.LGUICanvasScaler")
        local canvas_class = api:find_uobject("Class /Script/LGUI.LGUICanvas")
        if not valid(scaler_class) or not valid(canvas_class) then return false, "LGUI classes unavailable" end
        -- Uses UEVR's existing class index, never a global UObject-array scan.
        -- No UObject survives this request, and oversized sets are refused.
        local objects = hook.get_objects_by_class(scaler_class, false)
        if type(objects) ~= "table" or #objects > 64 then return false, "LGUI object set unavailable or too large" end
        local plans, reasons, order = {}, {}, {}
        for _, o in ipairs(objects) do
            if valid(o) and o:is_a(scaler_class) then
                local ok, plan, reason = pcall(candidate, o, context.world, canvas_class)
                if not ok then plan, reason = nil, "error " .. tostring(plan):sub(1, 60) end
                if plan then plans[#plans + 1] = plan
                else
                    reason = reason or "unknown"
                    if not reasons[reason] then reasons[reason] = 0; order[#order + 1] = reason end
                    reasons[reason] = reasons[reason] + 1
                end
            end
        end
        if #plans == 0 then
            local parts = {}
            for _, reason in ipairs(order) do parts[#parts + 1] = reasons[reason] .. "x " .. reason end
            s.last_rejections = #objects .. " scalers: " .. (#parts > 0 and table.concat(parts, ", ") or "none found")
            local ok, nudged = pcall(nudge, context, objects, scaler_class)
            if ok and nudged > 0 then
                local size = context.dims and (" at " .. context.dims[1] .. "x" .. context.dims[2]) or ""
                return true, "HUD scaler nudged (" .. nudged .. ")" .. size .. " (experiment); check the headset / HUD 已刷新（实验），请在头显中确认"
            end
            if not ok then s.last_rejections = s.last_rejections .. "; nudge error " .. tostring(nudged):sub(1, 80) end
            return false, "No active HUD with the supported LGUI schema"
        end
        local refreshed = 0
        for _, plan in ipairs(plans) do
            -- A getter or preceding canvas update could have changed the world.
            local now = current_context()
            if not now or address(now.world) ~= address(context.world) then return false, "World changed during refresh" end
            local fresh = candidate(plan.object, now.world, canvas_class)
            if not fresh or address(fresh.canvas) ~= address(plan.canvas) then return false, "HUD changed during refresh" end
            fresh.force(fresh.object)
            -- ForceUpdate may trigger layout callbacks. Revalidate both targets
            -- before the second write and use freshly read same-value parameters.
            now = current_context()
            if not now or address(now.world) ~= address(context.world) then return false, "World changed during refresh" end
            fresh = candidate(plan.object, now.world, canvas_class)
            if not fresh or address(fresh.canvas) ~= address(plan.canvas) then return false, "HUD changed during refresh" end
            fresh.set(fresh.canvas, table.unpack(fresh.values))
            refreshed = refreshed + 1
        end
        local size = context.dims and (" at " .. context.dims[1] .. "x" .. context.dims[2]) or ""
        return true, "HUD layout refreshed (" .. refreshed .. ")" .. size .. "; check the headset / HUD 已刷新，请在头显中确认"
    end
    local function save_schema_once(self)
        if self.schema_audited then return end
        self.schema_audited = true
        -- Only after the explicit/transition refresh already warmed both exact
        -- class lookups. One bounded, read-only report per Lua session; never
        -- probe repeatedly on idle ticks or weaken a rejected setter signature.
        local ok, result = pcall(function()
            local collect = require("wuwa_hud_schema")
            if type(collect) ~= "function" or not json or not json.dump_file then return nil end
            local report = collect({api = api, types = env.types})
            report.trigger = "unsupported-hud-refresh"
            local clock = env.clock and env.clock() or 0
            local tag = tostring(clock):gsub("[^%w_-]", "-"):sub(1, 48)
            local path = "diagnostics/hud-schema-auto-" .. tag .. ".json"
            if json.dump_file(path, report, 4) == false then return nil end
            local saved = json.load_file(path)
            if type(saved) ~= "table" or saved.trigger ~= report.trigger or saved.status ~= report.status then return nil end
            return path
        end)
        if ok and type(result) == "string" then
            self.schema_report = result
            self.status = self.status .. "; diagnostic saved / 已保存诊断"
        end
    end
    function s:request()
        self.pending = {manual = true, age = 0, ticks = 0, stable = 0}
        self.status = "HUD refresh queued / HUD 刷新已排队"
    end
    function s:reset()
        self.pending, self.last_mode, self.followups = nil, nil, nil
    end
    local function tick(self, mode, delta)
        if type(mode) ~= "boolean" then
            if self.pending then self.status = "HUD refresh unavailable: 2D state missing / 无法读取 2D 状态" end
            self.pending, self.last_mode = nil, nil
            return
        end
        if self.last_mode ~= nil and self.last_mode ~= mode then
            local manual = self.pending and self.pending.manual
            self.pending = {manual = manual, age = 0, ticks = 0, stable = 0}
            self.status = "Waiting for HUD dimensions / 等待 HUD 尺寸稳定"
            -- Test 3d: back in full VR the HUD could stay squashed until the ESC
            -- menu, so the first refresh may run before the new eye images are in
            -- use. Refresh again 1 s and 2.5 s after the switch (cheap, same calls).
            local started = env.clock and env.clock() or nil
            self.followups = {1.0, 2.5}
            self.follow_age, self.follow_start, self.follow_dims = 0, finite(started) and started or nil, nil
        end
        self.last_mode = mode
        if self.followups then
            self.follow_age = self.follow_age + (finite(delta) and math.max(0, math.min(delta, 0.25)) or 0)
            local now = env.clock and env.clock() or nil
            local elapsed = (self.follow_start and finite(now) and now >= self.follow_start) and now - self.follow_start or self.follow_age
            if not self.pending and elapsed >= self.followups[1] then
                -- Like the queued refresh, never enumerate while the size is still
                -- changing: the follow-up waits until it reads the same twice.
                local context = current_context()
                local dims = context and context.dims
                local key = dims and (dims[1] .. "x" .. dims[2]) or "none"
                if key ~= self.follow_dims then
                    self.follow_dims = key
                else
                    local at = table.remove(self.followups, 1)
                    if #self.followups == 0 then self.followups = nil end
                    local ok, message = refresh(context)
                    self.status = (ok and message or ("HUD refresh unavailable: " .. message)) .. " (follow-up at " .. at .. " s)"
                end
            end
        end
        local p = self.pending
        if not p then return end
        p.age = p.age + (finite(delta) and math.max(0, math.min(delta, 0.25)) or 0)
        p.ticks = p.ticks + 1
        local now = env.clock and env.clock() or nil
        local has_clock = finite(now) and now >= 0 and now <= 1e12
        if p.clock_used and (not has_clock or now < p.last_clock) then
            self.pending = nil; self.status = "HUD clock changed; retry reset / HUD 时钟已改变，请重试"; return
        end
        if has_clock then
            if not p.clock_used then p.started, p.settle_started = now, now end
            p.clock_used, p.last_clock = true, now
        end
        local elapsed = p.clock_used and now - p.started or p.age
        if elapsed >= 2 or p.ticks >= 120 then
            self.pending = nil; self.status = "HUD dimensions did not settle; retry reset / HUD 尺寸未稳定，请重试"; return
        end
        local context = current_context()
        if not context then self.pending = nil; self.status = "HUD refresh unavailable: no current world / 当前世界不可用"; return end
        local world = address(context.world)
        if p.world and p.world ~= world then
            self.pending = nil; self.status = "World changed; request HUD reset again / 世界已切换，请重试 HUD 重置"; return
        end
        p.world = world
        local dims = context.dims
        if not dims and not context.dimensions_available and not p.manual then
            self.pending = nil; self.status = "Automatic HUD refresh unavailable; use Reset HUD aspect / 自动刷新不可用，请手动重置 HUD 比例"; return
        end
        if dims then
            local unchanged = p.dims and dims[1] == p.dims[1] and dims[2] == p.dims[2]
            p.stable = unchanged and p.stable + 1 or 1
            if not unchanged then p.settle_started = now end
            p.dims = dims
        else
            p.stable, p.dims = 0, nil
            if context.dimensions_available then p.settle_started = now end
        end
        -- Native clock is monotonic even when dialogue pauses simulation. Wait
        -- 200ms after the last dimension change: three fast ticks alone can
        -- precede asynchronous target reallocation. Older backends have a
        -- bounded simulation-time/stable-tick fallback, not a GPU completion proof.
        local settled = p.clock_used and now - p.settle_started >= 0.2 - 1e-6
        if not p.clock_used then settled = p.age >= 0.2 or p.stable >= 12 or (not context.dimensions_available and p.manual and p.ticks >= 12) end
        if settled and ((dims and p.stable >= 2) or (not context.dimensions_available and p.manual)) then
            self.pending = nil
            local ok, message = refresh(context)
            self.status = ok and message or ("HUD refresh unavailable: " .. message .. " / HUD 刷新未完成")
            if not ok and message == "No active HUD with the supported LGUI schema" then
                if self.last_rejections then self.status = self.status .. " [" .. self.last_rejections .. "]" end
                save_schema_once(self)
            end
        end
    end
    function s:tick(mode, delta)
        -- Restore nudged scaler values on the tick after the nudge.
        if self.restore then
            if self.restore_armed then
                for _, r in ipairs(self.restore) do pcall(function() if valid(r.object) then r.set(r.object, r.value) end end) end
                self.restore, self.restore_armed = nil, nil
            else self.restore_armed = true end
        end
        local ok = pcall(tick, self, mode, delta)
        if not ok then self.pending = nil; self.status = "HUD refresh unavailable: reflection changed / 反射接口不可用，未继续刷新" end
    end
    return s
end
return M
