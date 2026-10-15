-- Close-up near clip (test 15) and hiding characters at your eyes (test 16):
-- what the camera script asks for, shot by shot.
-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_close_up_near_clip.lua mod/lua/02_WuWaVR_PolarControls.lua
local source=assert(arg[1], 'Pass the PolarControls script path')
local NOW='WuWaControls_NearClipNow'
local HIDE,SEEN='WuWaControls_HideNearCharacters','WuWaControls_NearSeen'
local Y=32768
local failures,passed={},0

-- A player camera: lens in degrees, the game's focus in cm (nil: no focus), at a fixed spot.
local function fixture()
    local f={callbacks={},errors={},logs={},sends={},clock=0}
    f.pp={bOverride_DepthOfFieldFocalDistance=true,DepthOfFieldFocalDistance=145,
        bOverride_DepthOfFieldFstop=true,DepthOfFieldFstop=2.3}
    f.pcm={fov=37.5,CameraCachePrivate={POV={PostProcessSettings=f.pp}}}
    function f.pcm:GetFOVAngle() return self.fov end
    function f.pcm:GetCameraLocation() return {X=-203,Y=223,Z=41} end
    function f.pcm:GetCameraRotation() return {Pitch=0,Yaw=5.5,Roll=0} end
    f.pc={bShowMouseCursor=false,PlayerCameraManager=f.pcm,get_class=function() return {find_function=function() end} end}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_CameraMode='0',
        WuWaControls_LensMatch='75',WuWaControls_LensNearest='10',WuWaControls_CloseUpNearClip='20',
        VR_2DScreenMode='false',VR_MonoTheatreMode='false',VR_EnableGUI='true',VR_WorldScale='1'}
    local env=setmetatable({uevr={
        api={get_player_controller=function() return f.pc end,get_local_pawn=function() end},
        params={vr={
            get_mod_value=function(_,key)
                if key=='WuWaControls_EffectiveMonoTheatre' then return f.values.VR_MonoTheatreMode end
                if key=='WuWaControls_EffectiveScreen' then return tostring(f.values.VR_2DScreenMode=='true' or f.values.VR_MonoTheatreMode=='true') end
                return f.values[key] or 'false'
            end,
            set_mod_value=function(key,value)
                if key==NOW then f.sends[#f.sends+1]=value end
                f.values[key]=value
            end},functions={is_drawing_ui=function() return false end,dispatch_custom_event=function() end,
                log_info=function(s) f.logs[#f.logs+1]=s end,
                log_warn=function(s) f.errors[#f.errors+1]=s end,log_error=function(s) f.errors[#f.errors+1]=s end}},
        sdk={callbacks=setmetatable({},{__index=function(_,name) return function(fn) f.callbacks[name]=fn end end})}},
        json={dump_string=function() return '{}' end}},{__index=_G})
    f.env=env
    assert(loadfile(source,'t',env))()
    -- One rendered frame: the engine tick (camera mode, view), then the draw callback where the lens runs.
    function f:frame()
        self.clock=self.clock+.016; self.values.WuWaControls_Clock=tostring(self.clock)
        self.callbacks.on_pre_engine_tick(nil,.016)
        self.callbacks.on_pre_viewport_client_draw()
        assert(#self.errors==0,self.errors[1])
        assert(not tostring(self.values.WuWaControls_LensStatus or ''):find('Unavailable'),self.values.WuWaControls_LensStatus)
        return tonumber(self.values[NOW] or '0')
    end
    function f:frames(n) local v; for _=1,n or 3 do v=self:frame() end; return v end
    -- One controller sample between frames; returns what the game receives.
    function f:pad(buttons,lt,rt)
        self.clock=self.clock+.016; self.values.WuWaControls_Clock=tostring(self.clock)
        local state={Gamepad={wButtons=buttons or 0,bLeftTrigger=lt or 0,bRightTrigger=rt or 0,
            sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0}}
        self.callbacks.on_xinput_get_state(nil,0,state,0)
        return state.Gamepad
    end
    return f
end
-- Characters around a game camera at the origin looking along +X (no focus, so no
-- close-up move): each has one skeletal mesh (bones in cm) and a held prop.
local function world(f)
    f.pcm.GetCameraLocation=function() return {X=0,Y=0,Z=0} end
    f.yaw=0
    f.pcm.GetCameraRotation=function() return {Pitch=0,Yaw=f.yaw,Roll=0} end
    f.pp.bOverride_DepthOfFieldFocalDistance=false
    f.values.WuWaControls_CloseUpNearClip='10'
    local classes,meshes={},{}
    local function class(path) classes[path]=classes[path] or {path=path}; return classes[path] end
    local api=f.env.uevr.api
    function api:find_uobject(path)
        if path=='Class /Script/Engine.KismetStringLibrary' then
            return {get_class_default_object=function() return {Conv_StringToName=function(_,s) return s end} end}
        end
        if path:find('Engine%.SkeletalMeshComponent') or path:find('Engine%.PrimitiveComponent') then return class(path) end
    end
    function api:to_uobject(address) return address end
    f.env.uevr.types={UObjectHook={get_objects_by_class=function(c)
        return c.path:find('Skeletal') and meshes or {}
    end}}
    local next_address=1000
    -- opts.parked: the actor is hidden (a party member off the field); opts.template:
    -- zero bounds, like a blueprint's component template.
    function f:person(name,bones,opts)
        opts=opts or {}
        next_address=next_address+10
        local owner={name=name,address=next_address,bHidden=opts.parked==true}
        function owner:get_address() return self.address end
        local function prim(main)
            local c={bRenderInMainPass=main,calls=0,address=next_address+#meshes+1}
            function c:get_address() return self.address end
            function c:SetRenderInMainPass(v) self.bRenderInMainPass=v; self.calls=self.calls+1 end
            return c
        end
        local m=prim(true); m.bones=bones; m.bVisible=true; m.bHiddenInGame=false
        -- World bounds follow the first bone, 60 cm around it.
        setmetatable(m,{__index=function(self,k)
            if k=='Bounds' then
                local b=self.bones[1]
                if opts.template then return {Origin={X=0,Y=0,Z=0},SphereRadius=0} end
                return {Origin={X=b.x,Y=b.y,Z=b.z},SphereRadius=60}
            end
        end})
        function m:get_outer() return owner end
        function m:GetNumBones() return #self.bones end
        function m:GetBoneName(i) return 'b'..i end
        function m:GetBoneIndex(n) local i=tonumber(tostring(n):match('^b(%d+)$')); return i and i<#self.bones and i or -1 end
        function m:GetSocketLocation(n) local b=self.bones[tonumber(n:sub(2))+1]; return {X=b.x,Y=b.y,Z=b.z} end
        owner.mesh,owner.prop,owner.off=m,prim(true),prim(false)   -- off: hidden by the game itself
        function owner:K2_GetComponentsByClass(c)
            if c.path:find('Skeletal') then return {self.mesh} end
            return {self.mesh,self.prop,self.off}
        end
        meshes[#meshes+1]=m
        return owner
    end
    return f
end
local function shown(o) return o.mesh.bRenderInMainPass and o.prop.bRenderInMainPass end
local function hidden(o) return not o.mesh.bRenderInMainPass and not o.prop.bRenderInMainPass end
-- Beside the left eye and out of the 16:9 frame (the shoulder in the 15 Oct recording).
local AT_EYE={{x=5,y=-25,z=0},{x=-10,y=-15,z=5},{x=2,y=-20,z=-10}}
-- As close, but one bone is in the flat shot: a deliberate close-up of that person.
local FRAMED={{x=5,y=-20,z=0},{x=25,y=0,z=0}}
local FAR={{x=300,y=0,z=0}}
local function test(name,body)
    local ok,message=pcall(body)
    if ok then passed=passed+1; print('PASS: '..name)
    else failures[#failures+1]=name..': '..tostring(message); print('FAIL: '..failures[#failures]) end
end
local function eq(got,want,what) assert(got==want,what..': got '..tostring(got)..', expected '..tostring(want)) end

test('a dialogue close-up asks for the setting every frame and logs it with the shot',function()
    local f=fixture()
    eq(f:frame(),20,'first frame of the shot')
    local before=#f.sends; f:frames(5)
    eq(#f.sends-before,5,'requests in five frames (a request lapses after a second)')
    local logged=false
    for _,line in ipairs(f.logs) do if line:find('%[WuWaLens%] shot=1') and line:find('near_clip=20') then logged=true end end
    assert(logged,'shot log line has no near_clip=20')
    assert(f.values.WuWaControls_LensStatus:find('hiding anything nearer than 20 cm',1,true),f.values.WuWaControls_LensStatus)
end)
test('it stays 10 cm short of the person the game focuses on, after the close-up move',function()
    local f=fixture()
    f.pp.DepthOfFieldFocalDistance=60      -- move 33 cm: 27 cm left, so 17 cm at most
    eq(f:frames(),16,'tight shot')
    f=fixture(); f.pp.DepthOfFieldFocalDistance=35   -- 15.5 cm left: no room above 10 cm
    eq(f:frames(),0,'very tight shot')
end)
test('the game own plane outside close-ups, on screens, in other camera modes and when set to 10',function()
    local f=fixture(); eq(f:frames(),20,'close-up')
    f.pcm.fov=75; eq(f:frames(),0,'wide shot')
    local sends=#f.sends; f:frames(4); eq(#f.sends,sends,'repeats of the off request')
    f.pcm.fov=37.5; eq(f:frames(),20,'next close-up')
    f.values.VR_2DScreenMode='true'; eq(f:frames(),0,'stereo screen')
    f.values.VR_2DScreenMode='false'; eq(f:frames(),20,'back in full VR')
    f.values.WuWaControls_CameraMode='1'; eq(f:frames(),0,'another camera mode')
    f.values.WuWaControls_CameraMode='0'; eq(f:frames(),20,'game camera again')
    f.values.WuWaControls_CloseUpNearClip='10'; eq(f:frames(),0,'setting at the game own 10 cm')
end)
test('it works with Close-up match off and in shots without a focus',function()
    local f=fixture(); f.values.WuWaControls_LensMatch='0'
    eq(f:frames(),20,'Close-up match off: no move, plane still raised')
    f=fixture(); f.pp.bOverride_DepthOfFieldFocalDistance=false
    eq(f:frames(),20,'no focus')
    local logged=false
    for _,line in ipairs(f.logs) do if line:find('subject=none') and line:find('near_clip=20') then logged=true end end
    assert(logged,'a shot without a focus is not logged')
end)
test('a raised setting is capped at 50 cm and the lens rows never ask below the game own plane',function()
    local f=fixture(); f.values.WuWaControls_CloseUpNearClip='80'; f.pp.DepthOfFieldFocalDistance=400
    eq(f:frames(),50,'capped setting')
    f.values.WuWaControls_CloseUpNearClip='3'; eq(f:frames(),0,'below the range')
end)

test('with hiding on, only a character at the eyes and out of the flat shot is hidden, prop included',function()
    local f=world(fixture()); f.values[HIDE]='true'
    local intruder,framed,far=f:person('intruder',AT_EYE),f:person('framed',FRAMED),f:person('far',FAR)
    f:frames(3)
    assert(hidden(intruder),'the character at the eye is still drawn')
    assert(shown(framed),'a character the flat shot shows was hidden')
    assert(shown(far),'a far character was hidden')
    eq(intruder.off.calls,0,'calls on a part the game itself hides')
    eq(intruder.mesh.calls,1,'calls to hide the mesh')
    local logged=false
    for _,line in ipairs(f.logs) do if line:find('%[WuWaHide%] shot=1 hiding cut at_eyes=1 in_frame=1 hidden=1') then logged=true end end
    assert(logged,'no hide log line for the shot')
    eq(f.values[SEEN],nil,'note request while hiding is on')
end)
test('a hidden character comes back in the flat shot, when switched off and when the close-up ends',function()
    local f=world(fixture()); f.values[HIDE]='true'
    local a=f:person('intruder',AT_EYE)
    f:frames(3); assert(hidden(a),'not hidden')
    a.mesh.bones={{x=5,y=-25,z=0},{x=40,y=0,z=0}}   -- steps into the shot
    f:frames(60); assert(shown(a),'still hidden after entering the flat shot')
    eq(a.off.calls,0,'calls on a part the game itself hides')
    a.mesh.bones=AT_EYE; f:frames(60); assert(hidden(a),'not hidden again at the eye')
    f.values[HIDE]='false'; f:frame(); assert(shown(a),'still hidden with the option off')
    f.values[HIDE]='true'; f:frames(2); assert(hidden(a),'not hidden with the option back on')
    f.pcm.fov=75; f:frame(); assert(shown(a),'still hidden after the close-up ended')
end)
test('at a cut a character still at the eye stays hidden; one that left comes back',function()
    local f=world(fixture()); f.values[HIDE]='true'
    local a=f:person('intruder',AT_EYE)
    f:frames(3); assert(hidden(a),'not hidden')
    f.yaw=15; f:frames(2); assert(hidden(a),'shown again at a cut while still at the eye')
    eq(a.mesh.calls,1,'calls on the mesh across the cut')
    a.mesh.bones={{x=5,y=-90,z=0},{x=0,y=-95,z=0}}
    f.yaw=0; f:frames(2); assert(shown(a),'still hidden after a cut that no longer qualifies it')
end)
test('with hiding off (the default) nothing is hidden and the menu is asked for its note',function()
    local f=world(fixture()); f.values.WuWaControls_NearHint='true'
    local a=f:person('intruder',AT_EYE)
    f:frames(3)
    assert(shown(a),'hidden with the option off'); eq(a.mesh.calls,0,'calls with the option off')
    eq(f.values[SEEN],'1','note request')
    f=world(fixture()); f.values.WuWaControls_NearHint='true'; f:person('framed',FRAMED); f:frames(3)
    eq(f.values[SEEN],nil,'note request for a framed character')
    f=world(fixture()); f.values.WuWaControls_NearHint='false'; f:person('intruder',AT_EYE); f:frames(3)
    eq(f.values[SEEN],nil,'note request with the note switched off')
end)
test('with the camera inside a character, bones under 10 cm deep do not count as in the flat shot',function()
    -- 15 Oct shot 9: a head filled the top of the right eye; its neck, 6 cm deep and 5 cm under
    -- the camera, counted as in the shot with the old fixed 10 cm margin, so it stayed.
    local f=world(fixture()); f.values[HIDE]='true'
    local inside=f:person('inside',{{x=12,y=2,z=14},{x=6,y=1,z=5},{x=-8,y=0,z=-20}})
    f:frames(3); assert(hidden(inside),'a character around the camera was kept')
end)
test('a hand in the flat shot keeps the character, head at the eye or not',function()
    local f=world(fixture()); f.values[HIDE]='true'
    local reaching=f:person('reaching',{{x=8,y=-12,z=10},{x=0,y=-20,z=-15},{x=60,y=10,z=-5}})
    f:frames(3); assert(shown(reaching),'a character with a hand in the flat shot was hidden')
end)
test('a parked party member and a blueprint template at the eye are left alone',function()
    local f=world(fixture()); f.values[HIDE]='true'
    local parked=f:person('parked',AT_EYE,{parked=true})
    local template=f:person('template',AT_EYE,{template=true})
    f:frames(3)
    eq(parked.mesh.calls+template.mesh.calls,0,'calls on a hidden actor or a template')
    f.values[HIDE]='false'; f.values.WuWaControls_NearHint='true'; f:frames(3)
    eq(f.values[SEEN],nil,'note request for a hidden actor or a template')
end)
test('meshes are read 64 a frame: a character is judged once the pass reaches it, crowd or not',function()
    local f=world(fixture()); f.values[HIDE]='true'
    for i=1,150 do f:person('far'..i,{{x=400+i,y=0,z=0}}) end
    local a=f:person('intruder',AT_EYE)
    f:frames(1); assert(shown(a),'judged before its mesh was read')   -- first pass: 64 of 151 meshes
    f:frames(2); assert(hidden(a),'not hidden once the pass reached it')
end)
test('LT + RT, then Y switches hiding; a Y meant for the game is left alone',function()
    local f=fixture()
    f:pad(); f:pad(0,255,255)
    local p=f:pad(Y,255,255)
    assert(p.wButtons & Y==0 and p.bLeftTrigger==0 and p.bRightTrigger==0,'the shortcut reached the game')
    f:frame(); eq(f.values[HIDE],'true','after the shortcut')
    p=f:pad(0,255,255); assert(p.bLeftTrigger==0 and p.bRightTrigger==0,'triggers leaked after Y was released')
    f:pad(); p=f:pad(0,255,0); eq(p.bLeftTrigger,255,'left trigger after the shortcut ended')
    f:pad(); f:pad(Y); p=f:pad(Y,255,255); f:frame()
    assert(p.wButtons & Y~=0,'a held Y was taken'); eq(f.values[HIDE],'true','after triggers pulled with Y held')
    f:pad(); p=f:pad(Y,255,255); f:frame()
    assert(p.wButtons & Y~=0,'Y with the triggers in the same sample was taken'); eq(f.values[HIDE],'true','after a simultaneous press')
    f:pad(); f:pad(0,255,255); f:pad(Y,255,255); f:frame(); eq(f.values[HIDE],'false','after the second shortcut')
end)

print(string.format('Close-up near clip tests passed: %d', passed))
if #failures>0 then error(#failures..' failed') end
