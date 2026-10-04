-- One-shot, read-only HUD schema audit. LuaLoader > Main > Run script.
-- Run after Reset HUD aspect has reported no supported HUD: the two exact
-- LGUI class lookups below then use the SDK's already-populated class cache.
-- No global object-array walk, setters, ForceUpdate calls, or input/settings writes.
local function diagnose(env)
    local api, report = env.api, {version = 1, status = "collecting", scalers = {}, canvases = {}, limit = 64}
    local function addr(o) return o and o:get_address() or nil end
    local function valid(o) return o ~= nil and api:to_uobject(addr(o)) ~= nil end
    local function kind(p) return p:get_class():get_fname():to_string() end
    local function identity(o)
        if not valid(o) then return {valid = false} end
        return {valid = true, address = string.format("%x", addr(o)), class = o:get_class():get_fname():to_string()}
    end
    local function in_world(o, world)
        for _ = 1, 16 do
            if not valid(o) then return false end
            if addr(o) == addr(world) then return true end
            o = o:get_outer()
        end
        return false
    end
    local function property(o, name, record)
        local p = o:get_class():find_property(name)
        record[name] = {present = p ~= nil, type = p and kind(p) or "missing"}
        if p and kind(p) == "ObjectProperty" then return o[name] end
        return nil
    end
    local function signature(o, name, expected, record)
        local fn = o:get_class():find_function(name)
        local info = {present = fn ~= nil, compatible = false, fields = {}, rejected = {}}
        record[name] = info
        if not fn then info.rejected[1] = "function missing"; return nil end
        info.size = fn:get_properties_size()
        local p = fn:get_child_properties()
        for i = 1, 12 do
            if not p then break end
            local n = p:get_fname():to_string()
            local typed = fn:find_property(n)
            local field = {name = n, type = kind(p), typed_present = typed ~= nil}
            if typed then
                field.typed_name, field.typed_type = typed:get_fname():to_string(), kind(typed)
                field.param, field.out, field.return_param, field.reference = typed:is_param(), typed:is_out_param(), typed:is_return_param(), typed:is_reference_param()
            end
            info.fields[#info.fields + 1] = field
            p = p:get_next()
        end
        if p then info.rejected[#info.rejected + 1] = "more than 12 fields or cyclic chain" end
        if #info.fields ~= #expected then info.rejected[#info.rejected + 1] = "parameter count differs" end
        if #expected == 0 and info.size ~= 0 then info.rejected[#info.rejected + 1] = "nonempty void parameter storage" end
        for i, spec in ipairs(expected) do
            local f = info.fields[i]
            if not f or f.name ~= spec[1] or f.type ~= spec[2] or f.typed_name ~= spec[1] or f.typed_type ~= spec[2]
                or f.param ~= true or f.return_param ~= (spec[1] == "ReturnValue")
                or (spec[1] ~= "ReturnValue" and (f.out ~= (spec[3] == "out") or (spec[3] ~= "out" and f.reference))) then
                info.rejected[#info.rejected + 1] = "field " .. i .. " differs from " .. spec[1] .. ":" .. spec[2]
            end
        end
        info.compatible = #info.rejected == 0
        return info.compatible and fn or nil
    end
    local returns = {bool = {{"ReturnValue", "BoolProperty"}}, object = {{"ReturnValue", "ObjectProperty"}},
        byte = {{"ReturnValue", "ByteProperty"}}, enum = {{"ReturnValue", "EnumProperty"}}, float = {{"ReturnValue", "FloatProperty"}}}
    local projection_args = {{"InProjectionType", "ByteProperty"}, {"InFovAngle", "FloatProperty"},
        {"InNearClipPlane", "FloatProperty"}, {"InFarClipPlane", "FloatProperty"}}
    local function reject(r, reason) r.rejected[#r.rejected + 1] = reason end
    local function getter(o, name, result, r)
        local fn = signature(o, name, returns[result], r.functions)
        if not fn then reject(r, name .. " signature unsupported"); return nil end
        return fn(o) -- Only zero-input, fully verified read-only getters reach here.
    end
    local function finite(n) return type(n) == "number" and n == n and math.abs(n) < math.huge end
    local function run()
        local engine = api:get_engine()
        assert(valid(engine), "current engine unavailable")
        report.context = {properties = {}, functions = {}}
        local viewport = property(engine, "GameViewport", report.context.properties)
        assert(valid(viewport), "GameViewport missing, invalid or not ObjectProperty")
        local world = property(viewport, "World", report.context.properties)
        assert(valid(world), "World missing, invalid or not ObjectProperty")
        report.context.world = identity(world)
        local pc = api:get_player_controller(0)
        if valid(pc) and in_world(pc, world) then
            local fn = signature(pc, "GetViewportSize", {{"SizeX", "IntProperty", "out"}, {"SizeY", "IntProperty", "out"}}, report.context.functions)
            if fn then local x, y = {}, {}; fn(pc, x, y); report.context.viewport_size = {x.result, y.result} end
        end
        local scaler_class = api:find_uobject("Class /Script/LGUI.LGUICanvasScaler")
        local canvas_class = api:find_uobject("Class /Script/LGUI.LGUICanvas")
        assert(valid(scaler_class) and valid(canvas_class), "previously-resolved LGUI classes unavailable")
        local function canvas_info(canvas)
            local r = {object = identity(canvas), rejected = {}, functions = {}}
            if not valid(canvas) then reject(r, "canvas invalid"); return r end
            r.expected_class, r.current_world = canvas:is_a(canvas_class), in_world(canvas, world)
            if not r.expected_class then reject(r, "not LGUICanvas"); return r end
            if not r.current_world then reject(r, "canvas outside current world"); return r end
            local root = getter(canvas, "GetRootCanvas", "object", r)
            r.root = identity(root)
            if addr(root) ~= addr(canvas) then reject(r, "not root canvas") end
            r.render_mode = getter(canvas, "GetRenderMode", "enum", r)
            if r.render_mode ~= 0 then reject(r, "render mode is not ScreenSpaceOverlay (0)") end
            -- Inspect this mutator's signature; never invoke it.
            if not signature(canvas, "SetProjectionParameters", projection_args, r.functions) then reject(r, "SetProjectionParameters signature unsupported") end
            return r
        end
        local objects = env.types.UObjectHook.get_objects_by_class(scaler_class, false)
        assert(type(objects) == "table", "scaler index unavailable")
        report.scaler_count = #objects
        assert(#objects <= 64, "scaler index exceeds 64-object limit")
        for _, o in ipairs(objects) do
            local r = {object = identity(o), rejected = {}, properties = {}, functions = {}, values = {}}
            report.scalers[#report.scalers + 1] = r
            if not valid(o) then reject(r, "scaler invalid")
            elseif not o:is_a(scaler_class) then reject(r, "not LGUICanvasScaler")
            elseif not in_world(o, world) then reject(r, "scaler outside current world")
            else
                r.current_world = true
                r.active = getter(o, "GetIsActiveAndEnable", "bool", r)
                if r.active ~= true then reject(r, "scaler inactive or active getter unavailable") end
                local canvas = property(o, "Canvas", r.properties)
                r.canvas = canvas_info(canvas)
                for _, reason in ipairs(r.canvas.rejected) do reject(r, "Canvas: " .. reason) end
                if not signature(o, "ForceUpdate", {}, r.functions) then reject(r, "ForceUpdate signature unsupported") end
                for _, field in ipairs({{"GetProjectionType", "byte"}, {"GetFovAngle", "float"}, {"GetNearClipPlane", "float"}, {"GetFarClipPlane", "float"}}) do
                    r.values[field[1]] = getter(o, field[1], field[2], r)
                end
                local v = r.values
                if v.GetProjectionType ~= 0 and v.GetProjectionType ~= 1 then reject(r, "projection type outside 0/1") end
                if not finite(v.GetFovAngle) or v.GetFovAngle <= 0 or v.GetFovAngle >= 360 then reject(r, "FOV invalid") end
                if not finite(v.GetNearClipPlane) or v.GetNearClipPlane <= 0 then reject(r, "near clip invalid") end
                if not finite(v.GetFarClipPlane) or not finite(v.GetNearClipPlane) or v.GetFarClipPlane <= v.GetNearClipPlane then reject(r, "far clip invalid") end
            end
            r.supported = #r.rejected == 0
        end
        objects = env.types.UObjectHook.get_objects_by_class(canvas_class, false)
        assert(type(objects) == "table", "canvas index unavailable")
        report.canvas_count = #objects
        assert(#objects <= 64, "canvas index exceeds 64-object limit")
        for _, o in ipairs(objects) do report.canvases[#report.canvases + 1] = canvas_info(o) end
        report.status = "complete"
    end
    local ok, error = pcall(run)
    if not ok then report.status, report.error = "stopped", tostring(error):sub(1, 512) end
    return report -- Only plain values escape; UObject/function handles do not.
end

-- Returning the collector also permits an isolated mock without loading UEVR.
if not uevr then return diagnose end
local done = false
uevr.sdk.callbacks.on_pre_engine_tick(function()
    if done then return end
    done = true
    local report = diagnose({api = uevr.api, types = uevr.types})
    local clock_ok, clock = pcall(function() return uevr.params.vr:get_mod_value("WuWaControls_Clock") end)
    local run_id = clock_ok and tostring(clock):gsub("[^%w_-]", "-") or "once"
    report.run_id = run_id
    local path = "diagnostics/hud-schema-" .. run_id:sub(1, 48) .. ".json"
    local ok, result = pcall(function() return json.dump_file(path, report, 4) end)
    local read_ok, saved = pcall(function() return json.load_file(path) end)
    if ok and result ~= false and read_ok and type(saved) == "table" and saved.run_id == run_id and saved.status == report.status then
        uevr.params.functions.log_info("WuWaHUDSchema: " .. report.status .. "; scalers=" .. tostring(report.scaler_count) .. "; canvases=" .. tostring(report.canvas_count) .. "; data/" .. path)
    else
        uevr.params.functions.log_error("WuWaHUDSchema: could not verify saved report at data/" .. path .. "; " .. tostring(result):sub(1, 160))
    end
end)
