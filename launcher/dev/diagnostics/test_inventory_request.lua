-- Pure endpoint fixture. No files, processes, real API calls, or game input.
local path=assert(arg[1])
local saved_time,saved_clock,saved_open=os.time,os.clock,io.open
local function copy(v)
    if type(v)~='table'then return v end
    local r={};for k,x in pairs(v)do r[k]=copy(x)end;return r
end
local function env()
    local e={now=1900000000,starts={},files={},opens=0,reads=0,closes=0,size=12}
    os.time=function()return e.now end;os.clock=function()return 1 end
    local hooks={}
    local inventory={busy=false,start=function(options)
        e.starts[#e.starts+1]=options;return e.start_ok~=false,'fixture_start_failure'
    end}
    package.loaded.wuwa_nearby_inventory=inventory
    local function fname(s)return{to_string=function()return s end}end
    local function object(addr,name,props,data)
        local cls={find_property=function(_,n)
            if props[n]then return{get_class=function()return{get_fname=function()return fname('ObjectProperty')end}end}end
        end}
        data=data or{};data.get_address=function()return addr end;data.get_full_name=function()return name end
        data.get_class=function()return cls end;return data
    end
    local world=object(10,'World Current',{});local pawn=object(11,'Pawn Current',{})
    local viewport=object(12,'Viewport',{World=true},{World=world})
    local engine=object(13,'Engine',{GameViewport=true},{GameViewport=viewport})
    e.world=world;e.pawn=pawn;e.viewport=viewport
    uevr={api={get_engine=function()return engine end,get_local_pawn=function()return e.pawn end,
        to_uobject=function(_,a)return a end,get_uobject_array=function()error('dormant endpoint scanned objects')end},
        sdk={callbacks={on_pre_engine_tick=function(f)assert(not hooks.tick);hooks.tick=f end,
            on_script_reset=function(f)hooks.reset=f end}},
        params={functions={log_info=function()end,log_warn=function(s)e.warning=s end}}}
    json={dump_file=function(p,v)
            assert(p=='diagnostics/nearby-static-meshes-session.json'or p=='diagnostics/nearby-static-meshes-receipt.json')
            if e.fail_save then return false end;e.files[p]=copy(v);return true
        end,load_file=function(p)return e.files[p]end,
        load_string=function(s)assert(s=='fixture-request');if e.malformed then return nil end;return copy(e.request)end}
    io.open=function(p,mode)
        assert(p=='WuWa-nearby-static-meshes.request.json'and mode=='r','unexpected file or mode')
        e.opens=e.opens+1;if not e.request then return nil end
        return{seek=function(_,where)if where=='end'then return e.size end;assert(where=='set');return 0 end,
            read=function(_,limit)assert(limit==4097);e.reads=e.reads+1;return e.grown and string.rep('x',4097)or'fixture-request'end,
            close=function()e.closes=e.closes+1 end}
    end
    assert(loadfile(path))()
    assert(#e.starts==0 and e.opens==0,'loading endpoint performed IO/scan')
    function e:poll()self.now=self.now+1;hooks.tick()end
    function e:again()hooks.tick()end
    function e:reset()hooks.reset()end
    function e:manifest()return self.files['diagnostics/nearby-static-meshes-session.json']end
    function e:receipt()return self.files['diagnostics/nearby-static-meshes-receipt.json']end
    function e:make(id)
        local m=self:manifest();local r=copy(m.expected)
        r.version=1;r.session_token=m.session_token;r.id=id or'request_0001'
        r.expires_unix=self.now+15;r.category='foliage';return r
    end
    e.inventory=inventory;e.hooks=hooks
    e:poll();assert(e:manifest()and #e.starts==0,'arming scan was not dormant')
    return e
end
local e=env()
local opens=e.opens;for i=1,20 do e:again()end;assert(e.opens==opens,'same-second disk polling')
assert(loadfile(path))();assert(#e.starts==0,'duplicate endpoint arm')
e.request=e:make();e:poll()
assert(#e.starts==1 and e:receipt().status=='consumed')
assert(e.starts[1].expected.world_address=='10'and e.starts[1].expected.pawn_address=='11')
e.starts[1].finished({id='output-id',saved=true,path='diagnostics/nearby-static-meshes-output.json',reason='scan_finished'})
assert(e:receipt().status=='finished'and e:receipt().id==e.request.id)
e:poll();assert(#e.starts==1,'leftover consumed request rearmed')
e:reset();e.request=e:make('request_0002');e:poll();assert(#e.starts==1,'reset endpoint rearmed')
print('PASS dormant, poll throttle, duplicate arm, consume/finish, repeated request, reset')
local bad={
    {'version',2},{'session_token','old-launch-token'},{'id','../bad.lua'},{'id','short'},
    {'id',string.rep('x',81)},{'expires_unix',1900000000},{'expires_unix',1900001000},
    {'expires_unix',1900000010.5},{'expires_unix',0/0},
    {'world_address','20'},{'pawn_address','21'},{'world_name','World Other'},{'category','eval'}
}
for _,pair in ipairs(bad)do
    e=env();e.request=e:make();e.request[pair[1]]=pair[2];e:poll()
    assert(#e.starts==0 and not e:receipt(),'invalid request accepted: '..pair[1])
end
print('PASS version, token, ID, expiry, world/pawn and category rejection')
for _,mode in ipairs({'oversize','grown','malformed','busy','failed_receipt'})do
    e=env();e.request=e:make()
    if mode=='oversize'then e.size=4097 elseif mode=='grown'then e.grown=true
    elseif mode=='malformed'then e.malformed=true elseif mode=='busy'then e.inventory.busy=true
    else e.fail_save=true end
    e:poll();assert(#e.starts==0,mode..' started a scan')
    if mode=='oversize'then assert(e.reads==0 and e.closes==1)end
    if mode=='grown'or mode=='malformed'then assert(e.closes==1)end
    if mode=='failed_receipt'then e.fail_save=false;e:poll();assert(#e.starts==0,'failed receipt auto retried consumed ID')end
end
print('PASS bounded reads, malformed JSON, busy gate and fail-closed receipt')
e=env();e.start_ok=false;e.request=e:make();e:poll()
assert(e:receipt().status=='finished'and e:receipt().saved==false and e:receipt().reason=='fixture_start_failure')
e=env();local previous=e:manifest();e.viewport.World=nil;e:poll();assert(#e.starts==0)
e.viewport.World=e.world;e.pawn={get_address=function()return 22 end};e:poll()
assert(e:manifest().expected.pawn_address=='22'and e:manifest().session_token==previous.session_token)
e.request=copy(previous.expected);e.request.version=1;e.request.session_token=previous.session_token
e.request.id='request_old1';e.request.expires_unix=e.now+15;e.request.category='all';e:poll();assert(#e.starts==0)
print('PASS refused start receipt and refreshed identity rejects previous pawn')
e=env()
for i=1,9 do e.request=e:make('request_'..string.format('%04d',i));e:poll()end
assert(#e.starts==8,'session request budget not enforced')
print('PASS eight-request lifetime cap')
os.time,os.clock,io.open=saved_time,saved_clock,saved_open
