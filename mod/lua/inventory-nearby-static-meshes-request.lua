-- Dormant optional autorun. Never evaluates arbitrary code or accepts a path.
-- Controller reads data/diagnostics/nearby-static-meshes-session.json, then
-- writes the fixed request below. It removes that exact file after consumption.
-- UEVR Lua disables os.remove; this entrypoint cannot delete requests itself.
local inventory=require("wuwa_nearby_inventory")
if inventory.endpoint_armed then return end
inventory.endpoint_armed=true
local api,cb,log=uevr.api,uevr.sdk.callbacks,uevr.params.functions
local REQUEST="WuWa-nearby-static-meshes.request.json"
local SESSION="diagnostics/nearby-static-meshes-session.json"
local RECEIPT="diagnostics/nearby-static-meshes-receipt.json"
local token=tostring(os.time()).."-"..tostring(math.floor(os.clock()*1000000)).."-"..tostring({}):gsub("[^%w]","")
local active,ready,last_poll,seen,count,advertised=true,false,-1,{},0,nil
local function current()
    local function alive(o)return o and api:to_uobject(o:get_address())~=nil end
    local function prop(o,n)
        if not alive(o)then return nil end
        local p=o:get_class():find_property(n)
        if p and p:get_class():get_fname():to_string()=="ObjectProperty"then return o[n]end
    end
    local world=prop(prop(api:get_engine(),"GameViewport"),"World")
    local pawn=api:get_local_pawn(0)
    if not alive(world)or not alive(pawn)then return nil end
    return {world_address=tostring(world:get_address()),pawn_address=tostring(pawn:get_address()),
        world_name=tostring(world:get_full_name())}
end
local function save(path,value)
    if json.dump_file(path,value,2)==false then return false end
    local check=json.load_file(path)
    return type(check)=="table" and check.session_token==token and check.id==value.id and check.status==value.status
end
cb.on_pre_engine_tick(function()
    if not active or os.time()==last_poll then return end
    last_poll=os.time() -- At most once a second; no per-frame disk polling.
    local ok,err=pcall(function()
        local identity=current()
        if not identity then return end
        if not ready or not advertised or advertised.world_address~=identity.world_address or
            advertised.pawn_address~=identity.pawn_address or advertised.world_name~=identity.world_name then
            ready=save(SESSION,{version=1,session_token=token,status="dormant",armed_unix=os.time(),
                request_name=REQUEST,expected=identity,maximum_requests=8})
            if ready then advertised=identity end
            if ready then log.log_info("WuWa mesh inventory request endpoint dormant; explicit matching request required.")end
        end
        if not ready or inventory.busy or count>=8 then return end
        local f=io.open(REQUEST,"r");if not f then return end
        local size=f:seek("end")
        if not size or size<2 or size>4096 then f:close();return end
        f:seek("set",0);local text=f:read(4097);f:close()
        if not text or #text>4096 then return end
        local r=json.load_string(text)
        if type(r)~="table"or r.version~=1 or r.session_token~=token or
            type(r.id)~="string"or #r.id<8 or #r.id>80 or not r.id:match("^[%w_-]+$")or seen[r.id] or
            type(r.expires_unix)~="number"or r.expires_unix%1~=0 or r.expires_unix<os.time() or r.expires_unix>os.time()+30 or
            r.world_address~=identity.world_address or r.pawn_address~=identity.pawn_address or r.world_name~=identity.world_name or
            (r.category~="foliage"and r.category~="all")then return end
        -- Consume before dispatch, including an eventual start/scan failure.
        seen[r.id]=true;count=count+1
        local accepted=save(RECEIPT,{version=1,session_token=token,id=r.id,status="consumed",consumed_unix=os.time()})
        if not accepted then return end
        local began,why=inventory.start({request_id=r.id,category=r.category,expected=identity,finished=function(result)
            result.version=1;result.session_token=token;result.id=r.id;result.status="finished"
            save(RECEIPT,result)
        end})
        if not began then save(RECEIPT,{version=1,session_token=token,id=r.id,status="finished",saved=false,reason=why})end
    end)
    if not ok then active=false;log.log_warn("WuWa inventory request endpoint stopped: "..tostring(err):sub(1,300))end
end)
cb.on_script_reset(function()active=false;inventory.endpoint_armed=false end)
