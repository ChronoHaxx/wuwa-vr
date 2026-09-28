-- Read-only implementation shared by explicit manual/request entrypoints.
-- Merely loading this module does not start an inventory.
-- No render proxy is treated as a UObject; no offsets, setters or input calls.
-- Output: this profile's data/diagnostics/nearby-static-meshes-<id>.json.
local M={busy=false,serial=0}
function M.start(options)
options=options or {}
if M.busy then return false,"busy" end
if options.category and options.category~="foliage" and options.category~="all" then return false,"category" end
M.busy=true
M.serial=M.serial+1
local api, cb, log = uevr.api, uevr.sdk.callbacks, uevr.params.functions
local LIMIT = {radius=6000, seconds=20, ticks=1200, objects=2000000,
    per_tick=4096, components=512, other_components=32, materials=16, instances=4096,
    retained_instances=256, payload_bytes=2097152}
local started, wall = os.clock(), os.time()
local id = tostring(wall).."-"..tostring(math.floor(started*1000000)).."-"..M.serial
local output = "diagnostics/nearby-static-meshes-"..id..".json"
local result = {version=1, kind="nearby_static_mesh_inventory", id=id, request_id=options.request_id,
    category=options.category or "foliage", read_only=true,
    limits=LIMIT, coordinate_space="current game world; not archive or rebased absolute coordinates",
    render_draw_identity_proven=false, components={}, errors={}, scanned=0, ticks=0,
    examined_instances=0, retained_instances=0, other_components=0, skipped_other_components=0, partial=true}
local active, array, pawn, world, mesh_class, bounds_lib, pending = true
local cursor, count, schema = 0, 0, {}
local function issue(text)
    if #result.errors<24 then result.errors[#result.errors+1]=tostring(text):sub(1,400) end
end
local function finite(v) return type(v)=="number" and v==v and math.abs(v)<1e12 end
local function vector(v)
    if not v then return nil end
    local x,y,z=v.x or v.X,v.y or v.Y,v.z or v.Z
    if finite(x) and finite(y) and finite(z) then return {x=x,y=y,z=z} end
end
local function address(obj) return obj and tostring(obj:get_address()) or nil end
local function valid(obj)
    return obj and api:to_uobject(obj:get_address())~=nil
end
local function identity(obj)
    if not valid(obj) then return nil end
    return {address=address(obj),name=tostring(obj:get_full_name()):sub(1,2048),
        class=tostring(obj:get_class():get_full_name()):sub(1,512)}
end
-- Check every reflected parameter name/type, including ReturnValue, before any
-- known read-only getter is invoked. Unsupported native signatures are skipped.
local function method(obj,name,expected)
    local cls=obj:get_class(); local key=address(cls)..":"..name
    if schema[key]~=nil then return schema[key] or nil end
    local fn=cls:find_function(name)
    if not fn then schema[key]=false; return nil end
    local field=fn:get_child_properties()
    for _,p in ipairs(expected) do
        if not field or field:get_fname():to_string()~=p[1] or
            field:get_class():get_fname():to_string()~=p[2] then
            issue("Unsupported getter signature: "..name); schema[key]=false; return nil
        end
        field=field:get_next()
    end
    if field then issue("Extra getter parameters: "..name); schema[key]=false; return nil end
    schema[key]=fn; return fn
end
local RET_VECTOR={{"ReturnValue","StructProperty"}}
local RET_INT={{"ReturnValue","IntProperty"}}
local RET_OBJ={{"ReturnValue","ObjectProperty"}}
local function prop(obj,name,kind)
    local p=obj:get_class():find_property(name)
    if p and p:get_class():get_fname():to_string()==kind then return obj[name] end
end
local function get_world()
    local engine=api:get_engine()
    if not valid(engine) then return nil end
    local viewport=prop(engine,"GameViewport","ObjectProperty")
    return valid(viewport) and prop(viewport,"World","ObjectProperty") or nil
end
local function in_world(obj)
    for _=1,16 do
        if not valid(obj) then return false end
        if obj:get_address()==world:get_address() then return true end
        obj=obj:get_outer()
    end
    return false
end
local function near(center,extent)
    if not center then return false end
    local d=0
    for _,k in ipairs({"x","y","z"}) do
        local v=math.max(0,math.abs(center[k]-result.pawn_location[k])-(extent and extent[k] or 0))
        d=d+v^2
    end
    return d<=LIMIT.radius^2
end
local function bounds(comp)
    local fn=method(bounds_lib,"GetComponentBounds",{{"Component","ObjectProperty"},
        {"Origin","StructProperty"},{"BoxExtent","StructProperty"},{"SphereRadius","FloatProperty"}})
    if not fn then return nil end
    local a,b,c={},{},{}; fn(bounds_lib,comp,a,b,c)
    local origin,extent=vector(a.result),vector(b.result)
    if origin and extent and extent.x>=0 and extent.y>=0 and extent.z>=0 and finite(c.result) then
        return {origin=origin,extent=extent,sphere_radius=c.result,source="GetComponentBounds"}
    end
end
local function finish(reason)
    if not active then return end
    active=false; result.reason=reason; result.partial=reason~="scan_finished"
    result.duration_seconds=os.clock()-started
    if pending then pending.row.instances_partial=true; pending=nil end
    result.component_count=#result.components
    local verified=false
    local ok,err=pcall(function()
        -- Measure the same compact encoding written below. Halve on overflow;
        -- both the retained count and cap termination remain explicit.
        local encoded=json.dump_string(result,-1)
        while #encoded>LIMIT.payload_bytes and #result.components>0 do
            local keep=math.floor(#result.components/2)
            while #result.components>keep do table.remove(result.components) end
            result.reason="payload_limit"; result.partial=true; result.component_count=#result.components
            encoded=json.dump_string(result,-1)
        end
        assert(#encoded>0 and #encoded<=LIMIT.payload_bytes,"Invalid or oversized inventory JSON")
        local saved=json.dump_file(output,result,-1)
        local check=json.load_file(output)
        verified=saved~=false and type(check)=="table" and check.id==id and
            check.reason==result.reason and check.scanned==result.scanned and
            check.component_count==result.component_count
    end)
    if not ok then result.save_error=tostring(err):sub(1,300) end
    pcall(log.log_info,"WuWa mesh inventory "..(verified and "saved: " or "SAVE FAILED: ")..output.." ("..result.reason..")")
    if options.finished then pcall(options.finished,{id=id,request_id=options.request_id,path=output,
        saved=verified,reason=result.reason,save_error=result.save_error,component_count=result.component_count}) end
    array,pawn,world,mesh_class,bounds_lib,schema,result=nil,nil,nil,nil,nil,nil,nil
    M.busy=false
end
local function inspect(comp)
    if not valid(comp) or not comp:is_a(mesh_class) or not in_world(comp) then return end
    if comp:get_class():get_class_default_object()==comp then return end
    local b=bounds(comp)
    if not b then issue("Component bounds unavailable: "..tostring(comp:get_full_name())); return end
    if not near(b.origin,b.extent) then return end
    local row=identity(comp); row.bounds=b; row.materials={}; row.instances={}
    local owner=method(comp,"GetOwner",RET_OBJ)
    if owner then row.owner=identity(owner(comp)) end
    local asset=prop(comp,"StaticMesh","ObjectProperty"); row.static_mesh=identity(asset)
    local loc=method(comp,"K2_GetComponentLocation",RET_VECTOR)
    if loc then row.location=vector(loc(comp)) end
    local getn=method(comp,"GetNumMaterials",RET_INT)
    local getm=method(comp,"GetMaterial",{{"ElementIndex","IntProperty"},{"ReturnValue","ObjectProperty"}})
    if getn and getm then
        local n=getn(comp)
        if finite(n) and n>=0 and n%1==0 then
            row.material_count=n; row.materials_partial=n>LIMIT.materials
            for i=0,math.min(n,LIMIT.materials)-1 do
                local mat=getm(comp,i); row.materials[#row.materials+1]={slot=i,material=identity(mat)}
            end
        end
    end
    local text=(row.static_mesh and row.static_mesh.name or ""):lower()
    for _,m in ipairs(row.materials) do text=text.." "..(m.material and m.material.name or ""):lower() end
    local hint=false
    for _,word in ipairs({"tree","foliage","impost","pivot","veget","forest","leaf","leaves","plant"}) do
        if text:find(word,1,true) then hint=true;break end
    end
    row.foliage_name_hint=hint -- A name filter, not asset-type or draw proof.
    if result.category=="foliage" and not hint then
        if result.other_components>=LIMIT.other_components then
            result.skipped_other_components=result.skipped_other_components+1; return
        end
        result.other_components=result.other_components+1
    end
    if #result.components>=LIMIT.components then finish("component_limit"); return end
    result.components[#result.components+1]=row
    local ni=method(comp,"GetInstanceCount",RET_INT)
    local ti=method(comp,"GetInstanceTransform",{{"InstanceIndex","IntProperty"},
        {"OutInstanceTransform","StructProperty"},{"bWorldSpace","BoolProperty"},{"ReturnValue","BoolProperty"}})
    if ni and ti then
        local n=ni(comp)
        if finite(n) and n>=0 and n%1==0 then
            row.instance_count=n; row.instances_examined=0; row.instances_partial=n>0
            pending={component=comp,row=row,index=0,count=n,fn=ti}
        end
    end
end
local function inspect_instance()
    local p=pending
    if not valid(p.component) then p.row.instances_error="component disappeared"; pending=nil; return end
    if p.index>=p.count then p.row.instances_partial=false; pending=nil; return end
    if result.examined_instances>=LIMIT.instances or result.retained_instances>=LIMIT.retained_instances then
        p.row.instances_error="global instance budget"; pending=nil; return
    end
    local out={}; local index=p.index; p.index=p.index+1
    result.examined_instances=result.examined_instances+1; p.row.instances_examined=p.index
    local ok=p.fn(p.component,index,out,true)
    if ok~=true or not out.result then return end
    local t=out.result; local pos=vector(t.Translation)
    if not near(pos) then return end
    local rot=t.Rotation; local q=rot and {x=rot.x or rot.X,y=rot.y or rot.Y,z=rot.z or rot.Z,w=rot.w or rot.W}
    if q and not (finite(q.x) and finite(q.y) and finite(q.z) and finite(q.w)) then q=nil end
    p.row.instances[#p.row.instances+1]={index=index,world_location=pos,world_rotation=q,
        world_scale=vector(t.Scale3D),bounds_available=false}
    -- A component's aggregate bounds must not be assigned to one instance.
    result.retained_instances=result.retained_instances+1
end
local initialized=false
cb.on_pre_engine_tick(function()
    if not active then return end
    local ok,err=pcall(function()
        result.ticks=result.ticks+1
        if os.time()-wall>=LIMIT.seconds or result.ticks>LIMIT.ticks then finish("time_limit"); return end
        if not initialized then
            pawn=api:get_local_pawn(0); world=get_world()
            assert(valid(pawn) and valid(world),"Current pawn/world unavailable")
            local posfn=assert(method(pawn,"K2_GetActorLocation",RET_VECTOR),"Pawn location getter unavailable")
            result.pawn=identity(pawn); result.world=identity(world)
            if options.expected then
                assert(result.pawn.address==options.expected.pawn_address and result.world.address==options.expected.world_address and
                    result.world.name==options.expected.world_name,"Request world/pawn no longer matches")
            end
            result.pawn_location=assert(vector(posfn(pawn)),"Invalid pawn location")
            mesh_class=assert(api:find_uobject("Class /Script/Engine.StaticMeshComponent"),"StaticMeshComponent unavailable")
            local klass=assert(api:find_uobject("Class /Script/Engine.KismetSystemLibrary"),"KismetSystemLibrary unavailable")
            bounds_lib=assert(klass:get_class_default_object(),"KismetSystemLibrary default unavailable")
            assert(method(bounds_lib,"GetComponentBounds",{{"Component","ObjectProperty"},{"Origin","StructProperty"},
                {"BoxExtent","StructProperty"},{"SphereRadius","FloatProperty"}}),"Component bounds contract unavailable")
            array=assert(api:get_uobject_array(),"Object array unavailable")
            count=array:get_object_count(); assert(finite(count) and count>=0 and count%1==0,"Invalid object count")
            result.initial_object_count=count; result.object_budget=math.min(count,LIMIT.objects)
            initialized=true
        end
        local current_world=get_world()
        local current_pawn=api:get_local_pawn(0)
        if not valid(pawn) or not valid(current_pawn) or current_pawn:get_address()~=pawn:get_address() or
            not valid(current_world) or current_world:get_address()~=world:get_address() then
            finish("world_or_pawn_changed"); return
        end
        local until_cpu=os.clock()+0.002
        for _=1,LIMIT.per_tick do
            if os.clock()>=until_cpu then break end
            if pending then inspect_instance()
            elseif cursor>=result.object_budget then
                finish(count>LIMIT.objects and "object_limit" or "scan_finished"); return
            else
                local i=cursor; cursor=cursor+1; result.scanned=cursor
                -- The array may shrink while streaming; never read past it.
                if i<array:get_object_count() then
                    local object=array:get_object(i)
                    local success,why=pcall(inspect,object)
                    if not success then issue(why) end
                    if not active then return end
                end
            end
        end
    end)
    if not ok and active then issue(err); finish("error") end
end)
cb.on_script_reset(function() if active then finish("script_reset") end end)
log.log_info("WuWa nearby mesh inventory armed once; maximum "..LIMIT.seconds.." seconds, no game changes.")
return true
end
return M
