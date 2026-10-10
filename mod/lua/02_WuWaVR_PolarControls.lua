-- Polar's Xbox layout, with frame-driven camera motion and focus-safe input.
-- No actors, view targets or saved world scale are changed. The optional head-shadow
-- mode owns bounded, non-colliding poseable components and destroys them on exit.
local api, vr, functions = uevr.api, uevr.params.vr, uevr.params.functions
local cb = uevr.sdk.callbacks
local B = {UP=1, DOWN=2, LEFT=4, RIGHT=8, MENU=16, VIEW=32, L3=64, R3=128,
    LB=256, RB=512, A=4096, B=8192, X=16384, Y=32768}
local slots, actions = {}, {}
local input_source=nil
local selected, selected_at, mode, prior_mode = nil, -100, 0, 0
local input, frame, base, free, owner, mesh_cache = nil, nil, nil, nil, nil, nil
local context_mouse, focused, camera_active = false, false, false
local game_menu = false
local status, last_error = "Polar controls ready; game camera unchanged.", nil
local mesh_mode, camera_mode, last_pawn, mesh_revision = -1, -1, nil, -1
local camera_late=false
local camera_motion=-1
local prior_motion=2
local stick_view_until=-100
local tick_sequence=0
local mesh_checked = -100
local acro_arm_requested=false
local collision_cache,collision_report,collision_error=nil,"",nil
local late_anchor_report="not sampled"
local fields = {"wButtons", "bLeftTrigger", "bRightTrigger", "sThumbLX", "sThumbLY", "sThumbRX", "sThumbRY"}
local function get(k) return vr:get_mod_value(k) end
local function enabled(k) local s=get(k); return s=="true" or s=="1" end
local function number(k, fallback, lo, hi)
    local n=tonumber(get(k)); if not n or n~=n then return fallback end
    return math.max(lo, math.min(hi,n))
end
local function now() return number("WuWaControls_Clock",0,0,1e12) end
local function set(k,v) vr.set_mod_value(k,tostring(v)) end
local function has(b,mask) return b & mask ~= 0 end
local function motion() return number("WuWaControls_FirstMotion",0,0,3) end
local function follow_animation() return motion()>=2 or (motion()==0 and enabled("WuWaControls_FollowHeadAnimation")) end
local function late_update() return motion()~=0 or enabled("WuWaControls_LateHeadUpdate") or number("WuWaControls_MeshMode",4,0,4)==4 end
local function axis(v) return math.abs(v)<8000 and 0 or v/32768 end
local function copy(p) local q={}; for _,k in ipairs(fields) do q[k]=p[k] end; return q end
local function neutral(p)
    return p.wButtons==0 and p.bLeftTrigger<30 and p.bRightTrigger<30 and
        axis(p.sThumbLX)==0 and axis(p.sThumbLY)==0 and axis(p.sThumbRX)==0 and axis(p.sThumbRY)==0
end
local function zero_pad(p) for _,k in ipairs(fields) do p[k]=0 end end
local function adjusting() return enabled("WuWaControls_AdjustMode") and enabled("WuWaControls_MouseAssist") end
local function utility_held(p)
    return p and enabled("WuWaControls_MouseAssist") and not adjusting() and
        has(p.wButtons,B.LB) and has(p.wButtons,B.R3) and not has(p.wButtons,B.L3)
end
local function point(p) return {x=p.x or p.X,y=p.y or p.Y,z=p.z or p.Z} end
local function valid(p)
    return p and type(p.x)=="number" and type(p.y)=="number" and type(p.z)=="number" and
        p.x==p.x and p.y==p.y and p.z==p.z and math.abs(p.x)<1e10 and math.abs(p.y)<1e10 and math.abs(p.z)<1e10
end
local function emit(p,utility,recenter,screenshot)
    functions.dispatch_custom_event("WuWaControls.Input.v1",json.dump_string({
        mouse=p~=nil and not has(p.wButtons,B.L3) and enabled("WuWaControls_MouseAssist") and
            (adjusting() or utility or (context_mouse and enabled("WuWaControls_AutoMouseMenus"))), utility=utility==true,
        buttons=p and p.wButtons or 0,x=p and p.sThumbLX or 0,y=p and p.sThumbLY or 0,
        menu=game_menu,recenter=recenter==true,screenshot=screenshot==true,status=status}))
end
local function alive(object)
    if not object then return false end
    local ok,value=pcall(function() return api:to_uobject(object:get_address()) end)
    return ok and value~=nil
end
local function has_function(object,name)
    return object and object:get_class():find_function(name)~=nil
end
local function destroy_shadow(entry)
    if entry.proxy and alive(entry.proxy) then
        pcall(function() entry.proxy:K2_DestroyComponent(entry.proxy) end)
    end
    entry.proxy=nil
    -- CastShadow belongs to the component, unlike the head-bone binding. A
    -- character can replace its asset on that same component; leaving our
    -- false flag there would make the incoming character permanently ineligible.
    if entry.shadow_cast~=nil and alive(entry.mesh) and entry.mesh.CastShadow==false then
        pcall(function() entry.mesh:SetCastShadow(entry.shadow_cast) end)
    end
    entry.shadow_cast=nil
end
local function restore_mesh()
    if not owner then return end
    local saved=owner; owner=nil
    for _,entry in ipairs(saved) do
        destroy_shadow(entry)
        if alive(entry.mesh) then
            local ok,err=pcall(function()
                -- Never unhide a replacement skeleton that reused the same component.
                if entry.bone and entry.asset and entry.mesh.SkeletalMesh~=entry.asset then return end
                if entry.bone then
                    if not entry.was_hidden then entry.mesh:UnHideBoneByName(entry.bone) end
                else
                    if entry.mesh.bHiddenInGame==true then entry.mesh:SetHiddenInGame(entry.was_hidden,false) end
                    for name,value in pairs(entry.properties or {}) do entry.mesh[name]=value end
                end
            end)
            if not ok then functions.log_warn("WuWaControls visibility restore: "..tostring(err)) end
        end
    end
end
local function shadow_caster(mesh)
    return mesh:get_class():find_property("CastShadow") and mesh.CastShadow==true and
        not mesh.bHiddenInGame and (not mesh:get_class():find_property("bVisible") or mesh.bVisible==true)
end
local function update_head_shadow(entry,pawn)
    local source=entry.mesh
    -- Character swaps often reveal/enable the new component after binding it.
    -- Re-evaluate readiness instead of freezing its transitional flags forever.
    -- With a live copy, CastShadow=false is our own replacement write.
    local visible=not source.bHiddenInGame and
        (not source:get_class():find_property("bVisible") or source.bVisible==true)
    local eligible=visible and not entry.was_hidden and
        (entry.shadow_cast==true or shadow_caster(source))
    if not eligible then
        if entry.proxy then destroy_shadow(entry) end
        entry.shadow_ready=false
        return
    end
    if not entry.shadow_ready then
        entry.shadow_ready=true; entry.shadow_retry=nil; entry.shadow_attempts=0
    end
    if entry.shadow_retry and now()<entry.shadow_retry then return end
    local ok,err=pcall(function()
        if not entry.proxy then
            local klass=api:find_uobject("Class /Script/Engine.PoseableMeshComponent")
            assert(klass and has_function(pawn,"FinishAddComponent"),"Poseable shadow components unavailable")
            local template=klass:get_class_default_object()
            -- Check destruction before allocating an owned engine component.
            for _,name in ipairs({"K2_DestroyComponent","SetSkeletalMesh","CopyPoseFromSkeletalComponent","SetHiddenInGame",
                "SetCollisionEnabled","SetGenerateOverlapEvents","SetMaterial","GetNumBones","K2_SetWorldTransform","SetCastShadow"}) do
                assert(has_function(template,name),"Shadow component missing "..name)
            end
            assert(template:get_class():find_property("bCastHiddenShadow"),"Hidden shadow unsupported")
            for _,name in ipairs({"K2_GetComponentToWorld","GetNumBones","GetNumMaterials","GetMaterial","SetCastShadow"}) do
                assert(has_function(source,name),"Shadow source missing "..name)
            end
            local proxy=api:add_component_by_class(pawn,klass,true)
            assert(proxy and alive(proxy),"Shadow component creation failed")
            entry.proxy=proxy
            for _,name in ipairs({"K2_DestroyComponent","SetSkeletalMesh","CopyPoseFromSkeletalComponent","SetHiddenInGame",
                "SetCollisionEnabled","SetGenerateOverlapEvents","SetMaterial","GetNumBones","K2_SetWorldTransform","SetCastShadow"}) do
                assert(has_function(proxy,name),"Shadow component missing "..name)
            end
            assert(proxy:get_class():find_property("bCastHiddenShadow"),"Hidden shadow unsupported")
            proxy:SetHiddenInGame(true,false)
            proxy:SetCollisionEnabled(0); proxy:SetGenerateOverlapEvents(false)
            proxy:SetSkeletalMesh(entry.asset,false)
            assert(proxy:GetNumBones()==source:GetNumBones() and source:GetNumBones()>0,"Shadow skeleton mismatch")
            local count=source:GetNumMaterials(); assert(count>=0 and count<=64,"Unsupported material count")
            for i=0,count-1 do proxy:SetMaterial(i,source:GetMaterial(i)) end
            for _,name in ipairs({"bCastDynamicShadow","bCastStaticShadow","bCastInsetShadow","bCastContactShadow","bCastFarShadow"}) do
                if source:get_class():find_property(name) and proxy:get_class():find_property(name) then proxy[name]=source[name] end
            end
            proxy.bCastHiddenShadow=true
            pawn:FinishAddComponent(proxy,true,source:K2_GetComponentToWorld())
            proxy:SetCastShadow(true)
        end
        assert(alive(entry.proxy),"Shadow component disappeared")
        entry.proxy:K2_SetWorldTransform(source:K2_GetComponentToWorld(),false,{},true)
        entry.proxy:CopyPoseFromSkeletalComponent(source)
        -- Replace this source's shadow, never turn on an unused equipment rig.
        if entry.shadow_cast==nil then entry.shadow_cast=source.CastShadow end
        source:SetCastShadow(false)
    end)
    if not ok then
        destroy_shadow(entry)
        entry.shadow_attempts=math.min((entry.shadow_attempts or 0)+1,6)
        entry.shadow_retry=now()+0.25*2^(entry.shadow_attempts-1)
        if entry.shadow_error~=tostring(err) then
            functions.log_warn("WuWaControls head shadow waiting; automatic retry: "..tostring(err))
        end
        entry.shadow_error=tostring(err)
    else
        entry.shadow_error=nil; entry.shadow_retry=nil; entry.shadow_attempts=0
    end
end
local head_names={"Head","head","Bip001 Head","Bip001_Head","Bip001Head","Bip01_Head","Head_M"}
local function head_bone(mesh,lib)
    if not has_function(mesh,"GetBoneIndex") or not has_function(mesh,"GetSocketLocation") then return end
    for _,name in ipairs(head_names) do
        local bone=lib:Conv_StringToName(name)
        if mesh:GetBoneIndex(bone)>=0 then return bone,name end
    end
    -- Namespaced rigs are discovered once, not scanned in the render/input callbacks.
    -- Deliberately exclude head-end, cloth and other similarly named helper bones.
    if has_function(mesh,"GetNumBones") and has_function(mesh,"GetBoneName") then
        for i=0,math.min(mesh:GetNumBones(),1024)-1 do
            local bone=mesh:GetBoneName(i)
            local name=bone:to_string()
            local tail=name:match("([^:|]+)$") or name
            local canonical=tail:lower():gsub("[%s_]","")
            if canonical=="head" or canonical=="headm" or canonical=="bip001head" or canonical=="bip01head" then
                return bone,name
            end
        end
    end
end
local function character_mesh(pawn)
    local mesh=pawn:get_class():find_property("Mesh") and pawn.Mesh or nil
    if not mesh or not alive(mesh) then return end
    return mesh,mesh:get_class():find_property("SkeletalMesh") and mesh.SkeletalMesh or nil
end
local function matches_rig(info,main,asset)
    return info and info.character_mesh==main and info.character_asset==asset and
        (not info.mesh or alive(info.mesh)) and
        (not info.primary or not info.primary.asset or info.mesh.SkeletalMesh==info.primary.asset)
end
local function find_mesh(pawn)
    local t=now()
    local previous=mesh_cache and mesh_cache.pawn==pawn and mesh_cache or nil
    local main,main_asset=character_mesh(pawn)
    -- Accessory discovery can be throttled. The Character's active mesh cannot:
    -- during a swap its previous component may remain a valid, live UObject.
    if matches_rig(previous,main,main_asset) and t-previous.checked<1 then return previous end
    local next_cache={pawn=pawn,checked=t,entries={},revision=(previous and previous.revision or 0)+1,
        character_mesh=main,character_asset=main_asset}
    local klass=api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
    local lib=api:find_uobject("Class /Script/Engine.KismetStringLibrary")
    if not klass or not lib then mesh_cache=next_cache; return next_cache end
    lib=lib:get_class_default_object(); local meshes,seen={},{}
    local function add(mesh)
        if mesh and alive(mesh) and not seen[mesh:get_address()] and #meshes<32 then
            seen[mesh:get_address()]=true; meshes[#meshes+1]=mesh
        end
    end
    -- Prefer the Character's main mesh over an auxiliary face/hair component.
    add(main)
    local all
    if has_function(pawn,"K2_GetComponentsByClass") then all=pawn:K2_GetComponentsByClass(klass)
    elseif has_function(pawn,"GetComponentsByClass") then all=pawn:GetComponentsByClass(klass) end
    for _,mesh in ipairs(all or {}) do add(mesh) end
    if #meshes==0 and has_function(pawn,"GetComponentByClass") then add(pawn:GetComponentByClass(klass)) end
    for _,mesh in ipairs(meshes) do
        local asset=mesh:get_class():find_property("SkeletalMesh") and mesh.SkeletalMesh or nil
        local entry
        for _,old in ipairs(previous and previous.entries or {}) do
            if old.mesh==mesh and old.asset==asset then entry=old; break end
        end
        if not entry then
            local bone,name=head_bone(mesh,lib)
            entry={mesh=mesh,asset=asset,bone=bone,name=name}
        end
        next_cache.entries[#next_cache.entries+1]=entry
        if entry.bone and not next_cache.mesh then
            next_cache.mesh=mesh; next_cache.bone=entry.bone; next_cache.name=entry.name
            next_cache.primary=entry
        end
    end
    if not next_cache.mesh then next_cache.mesh=meshes[1] end
    local same=previous and #previous.entries==#next_cache.entries
    if same then for i,entry in ipairs(next_cache.entries) do if entry~=previous.entries[i] then same=false; break end end end
    if previous and next_cache.primary and next_cache.primary==previous.primary then
        next_cache.anchor=previous.anchor; next_cache.parent_anchor=previous.parent_anchor
        next_cache.rotation_reference=previous.rotation_reference
        next_cache.look_blend=previous.look_blend
    end
    if same then
        next_cache.revision=previous.revision
    else
        restore_mesh(); mesh_mode=-1
    end
    mesh_cache=next_cache
    return mesh_cache
end
local function update_mesh(pawn,after_animation)
    local choice=number("WuWaControls_MeshMode",4,0,4)
    if mode~=3 or not camera_active or context_mouse then restore_mesh(); mesh_mode=-1; return end
    local info=find_mesh(pawn)
    if camera_late and (choice==2 or choice==4) and not info.anchor then
        -- Hiding a bone can collapse its socket. Keep it available until a
        -- post-simulation sample has established the camera/parent offset.
        restore_mesh(); mesh_mode=-1
        status="First person: waiting for a visible head pose before drawing; head not hidden yet."
        return
    end
    local changed=mesh_mode~=choice or mesh_revision~=info.revision or (choice~=0 and not owner)
    if choice~=4 and not changed and now()-mesh_checked<0.5 then return end
    mesh_checked=now()
    if changed then restore_mesh(); mesh_mode=choice; mesh_revision=info.revision end
    if choice==0 then status="First person: entire character visible. "..(info.anchor_report or "anchor=unmeasured"); return end
    if changed then
        owner={}
        for _,entry in ipairs(info.entries) do
            local mesh=entry.mesh
            if alive(mesh) then
                if (choice==1 or choice==3) and mesh:get_class():find_property("bHiddenInGame") and has_function(mesh,"SetHiddenInGame") then
                    local saved={mesh=mesh,asset=entry.asset,was_hidden=mesh.bHiddenInGame,properties={}}
                    if choice==3 then
                        -- Hiding the component leaves its head bones intact.
                        -- Bone-scale hiding cannot cast a complete head shadow.
                        -- Preserve the game's caster selection. Enabling CastShadow
                        -- on every attached rig also revives unused wing/effect rigs.
                        for name,value in pairs(shadow_caster(mesh) and
                            {bCastHiddenShadow=true,VisibilityBasedAnimTickOption=0,bEnableUpdateRateOptimizations=false} or {}) do
                            if mesh:get_class():find_property(name) then
                                saved.properties[name]=mesh[name]
                            end
                        end
                    end
                    owner[#owner+1]=saved
                elseif (choice==2 or choice==4) and entry.bone and has_function(mesh,"IsBoneHiddenByName") and
                    has_function(mesh,"HideBoneByName") and has_function(mesh,"UnHideBoneByName") then
                    owner[#owner+1]={mesh=mesh,asset=entry.asset,bone=entry.bone,was_hidden=mesh:IsBoneHiddenByName(entry.bone)}
                end
            end
        end
    end
    local verified=0
    for _,saved in ipairs(owner or {}) do
        local mesh=saved.mesh
        if alive(mesh) and (not saved.asset or mesh.SkeletalMesh==saved.asset) then
            if saved.bone then
                -- If the game reveals a previously hidden bone during a swap,
                -- only then may this mode take ownership of hiding it again.
                if saved.was_hidden and not mesh:IsBoneHiddenByName(saved.bone) then saved.was_hidden=false end
                if choice==4 and after_animation then update_head_shadow(saved,pawn) end
                if not mesh:IsBoneHiddenByName(saved.bone) then mesh:HideBoneByName(saved.bone,0) end
                if mesh:IsBoneHiddenByName(saved.bone) then verified=verified+1 end
            else
                if choice==3 then
                    for name,_ in pairs(saved.properties) do
                        mesh[name]=name=="VisibilityBasedAnimTickOption" and 0 or name~="bEnableUpdateRateOptimizations"
                    end
                end
                if not mesh.bHiddenInGame then mesh:SetHiddenInGame(true,false) end
                if mesh.bHiddenInGame then verified=verified+1 end
            end
        end
    end
    -- Function availability/a successful call is not proof the engine accepted
    -- the change. Recheck at a bounded rate without losing the original state.
    status=(choice==2 or choice==4) and string.format("First person: %d/%d supported head bones report hidden (%d components); body kept. %s",verified,#owner,#info.entries,info.anchor_report or "anchor=unmeasured")
        or string.format("First person: %d/%d skeletal components report hidden. %s",verified,#owner,info.anchor_report or "anchor=unmeasured")
    if #owner==0 then status="First person: no supported mesh/head found; character kept visible. "..(info.anchor_report or "anchor=unmeasured") end
    if choice==3 then status=status.." Full-body hidden shadow requested; visual result unverified." end
    if choice==4 then
        local proxies,errors=0,0
        for _,entry in ipairs(owner or {}) do
            if entry.proxy then proxies=proxies+1 end
            if entry.shadow_error then errors=errors+1 end
        end
        status=status..string.format(" Full-shadow copies: %d; retrying: %d (experimental).",proxies,errors)
    end
    if motion()==3 then status=status.." "..(info.rotation_report or "Animated rotation not sampled yet.") end
end
local function choose_camera(next_mode)
    if next_mode==2 or next_mode==3 then prior_mode=mode==1 and 1 or 0 end
    set("WuWaControls_CameraMode",next_mode)
end
local function toggle_camera(which) choose_camera(mode==which and prior_mode or which) end
local function pawn_yaw(pawn)
    if has_function(pawn,"K2_GetActorRotation") then
        local r=pawn:K2_GetActorRotation()
        return math.rad(r.Yaw or r.yaw or r.y or r.Y or 0)
    end
    return 0
end
local function bone_hidden(mesh,bone)
    return has_function(mesh,"IsBoneHiddenByName") and mesh:IsBoneHiddenByName(bone)
end
local function parent_calibration(info,socket)
    if not has_function(info.mesh,"GetParentBone") or not has_function(info.mesh,"GetSocketTransform") then return end
    local klass=api:find_uobject("Class /Script/Engine.KismetMathLibrary")
    local mathlib=klass and klass:get_class_default_object()
    if not has_function(mathlib,"InverseTransformLocation") or not has_function(mathlib,"TransformLocation") then return end
    local bone=info.bone
    for _=1,8 do
        bone=info.mesh:GetParentBone(bone)
        local name=bone and (type(bone)=="string" and bone or bone:to_string()) or "None"
        if name=="None" or name=="" or info.mesh:GetBoneIndex(bone)<0 then return end
        if not bone_hidden(info.mesh,bone) then
            -- RTS_World=0. Let the engine handle rotation AND scale instead of
            -- assuming a particular character's rig, height or sprint posture.
            local transform=info.mesh:GetSocketTransform(bone,0)
            local offset=transform and mathlib:InverseTransformLocation(transform,socket)
            local p=offset and point(offset)
            if valid(p) then return {bone=bone,name=name,offset=p,mathlib=mathlib} end
            return
        end
    end
end
local function animated_parent(info)
    local anchor=info.parent_anchor
    if not anchor or bone_hidden(info.mesh,anchor.bone) then return end
    if not alive(anchor.mathlib) then return end
    local transform=info.mesh:GetSocketTransform(anchor.bone,0)
    if not transform then return end
    -- Reflected FVector parameters require the full Lua binding. The API's
    -- UEVR_Vector3f wrapper is a distinct type and is rejected by set_property.
    local offset=Vector3f.new(anchor.offset.x,anchor.offset.y,anchor.offset.z)
    local result=anchor.mathlib:TransformLocation(transform,offset)
    return result and point(result)
end
local function first_person_anchor(pawn,pos,known_mesh,allow_calibration)
    local info=known_mesh or find_mesh(pawn)
    local yaw=pawn_yaw(pawn)
    local animated=follow_animation()
    local hidden=info.bone and bone_hidden(info.mesh,info.bone)
    local visibility=number("WuWaControls_MeshMode",4,0,4)
    local hiding_head=visibility==2 or visibility==4 or hidden
    -- HideBoneByName collapses bones; do not use their altered position as a camera anchor.
    if allow_calibration~=false and info.bone and not hidden and (not info.anchor or (animated and not hiding_head)) then
        local socket=info.mesh:GetSocketLocation(info.bone)
        local head=socket and point(socket)
        if valid(head) then
            local dx,dy=head.x-pos.x,head.y-pos.y
            if not info.anchor then
                info.anchor={x=math.cos(yaw)*dx+math.sin(yaw)*dy,
                    y=-math.sin(yaw)*dx+math.cos(yaw)*dy,z=head.z-pos.z}
                functions.log_info(string.format("[WuWaCameraAnchor] pawn=%s mesh=%s head=%s local_xyz=%.1f,%.1f,%.1f",
                    tostring(pawn:get_address()),tostring(info.mesh:get_address()),info.name or "unknown",
                    info.anchor.x,info.anchor.y,info.anchor.z))
                local ok,parent=pcall(parent_calibration,info,socket)
                if ok then info.parent_anchor=parent
                else functions.log_warn("WuWaControls parent calibration unavailable: "..tostring(parent)) end
            end
            if animated and not hiding_head then
                info.anchor_report=string.format("anchor=animated-head xyz=%.1f,%.1f,%.1f",dx,dy,head.z-pos.z)
                return head
            end
        end
    end
    if animated and info.parent_anchor then
        local ok,head=pcall(animated_parent,info)
        if ok and valid(head) then
            info.anchor_report=string.format("anchor=animated-parent:%s xyz=%.1f,%.1f,%.1f",
                info.parent_anchor.name:sub(1,20),head.x-pos.x,head.y-pos.y,head.z-pos.z)
            return head
        end
        if not ok and info.animation_error~=tostring(head) then
            info.animation_error=tostring(head)
            functions.log_warn("WuWaControls animated anchor unavailable: "..info.animation_error)
        end
    end
    if info.anchor then
        local a=info.anchor
        info.anchor_report=string.format("anchor=head:%s xyz=%.1f,%.1f,%.1f",(info.name or "unknown"):sub(1,24),a.x,a.y,a.z)
        if animated then info.anchor_report=info.anchor_report.." (animation unavailable)" end
        pos.x=pos.x+math.cos(yaw)*a.x-math.sin(yaw)*a.y
        pos.y=pos.y+math.sin(yaw)*a.x+math.cos(yaw)*a.y
        pos.z=pos.z+a.z
    else
        pos.z=pos.z+80
        info.anchor_report="anchor=fallback xyz=0,0,80"
        status="First person: no verified head anchor; using adjustable pawn-height fallback."
    end
    return pos
end

local function choose_input_source(source)
    if input_source~=source then
        input_source=source; slots={}; actions={}; selected=nil; input=nil
    end
end
local function process_pad(index,state,result)
    -- New native callback provides a value copy of the result, avoiding a raw
    -- pointer read. A disconnected pad's undefined state must never be consumed.
    if result~=0 or not state then
        slots[index]=nil
        if selected==index then input=nil; selected=nil; actions={} end
        return
    end
    local p=copy(state.Gamepad)
    if not enabled("WuWaControls_Enabled") or not enabled("WuWaControls_Focused") or
        enabled("VR_WuWaGamepadPassthrough") or functions.is_drawing_ui() then
        slots={}; actions={}; selected=nil; input=nil; return
    end
    local t=now()
    local s=slots[index] or {previous=0,armed=false,tap_l=-100,tap_r=-100,tap_rt=-100,adjust_seen=false}
    slots[index]=s
    if not s.armed then
        s.armed=neutral(p); s.previous=p.wButtons
        s.adjust_seen=adjusting()
        if s.armed and (selected==nil or selected==index) then input={pad=p,at=t} end
        if adjusting() and not (has(p.wButtons,B.L3) and has(p.wButtons,B.R3)) then zero_pad(state.Gamepad) end
        return
    end
    if selected~=index then
        if neutral(p) then return end
        if selected~=nil and t-selected_at<1 then return end
        actions={}; selected=index
    end
    if not neutral(p) then selected_at=t end
    input={pad=p,at=t}
    local b,previous=p.wButtons,s.previous
    local rising=b & (~previous)
    local l3,r3,lb=has(b,B.L3),has(b,B.R3),has(b,B.LB)
    if l3 and r3 then
        actions.view=nil
        if s.view_chord then s.view_chord="cancelled" end
        s.mono_ready=nil
        s.tap_l=-100; s.tap_r=-100; s.lone_l=false; s.lone_r=false; s.previous=b; input=nil; return
    end
    local adjust=adjusting()
    if s.adjust_seen~=adjust then s.adjust_seen=adjust; s.adjust_wait=true end
    -- Enter/leave only after all controls are released. This also protects
    -- menu-click/game-action transitions and a checkbox changed in UEVR.
    if s.adjust_wait or actions.adjust then
        s.mono_ready=nil; actions.view=nil
        input=nil; zero_pad(state.Gamepad); s.previous=b
        if neutral(p) and not actions.adjust then s.adjust_wait=false; input={pad=p,at=t} end
        return
    end
    if l3 and lb and (previous&(B.L3|B.LB))~=(B.L3|B.LB) and enabled("WuWaControls_MouseAssist") then
        actions.adjust=true; s.adjust_wait=true; s.lone_l=false; s.lone_r=false
        input=nil; zero_pad(state.Gamepad); s.previous=b; return
    end
    -- Mono theatre requires a prior, fresh sample with both full triggers and
    -- neither stick pressed. Simultaneous input or R3-first cannot arm it.
    local mono_ready=s.mono_ready and t>=s.mono_ready and t-s.mono_ready<=0.25
    if not s.view_chord and not adjust and b==0 and p.bLeftTrigger>=180 and p.bRightTrigger>=180 then
        s.mono_ready=t
    else s.mono_ready=nil end
    -- One shared latch owns portal, diorama, stereo-screen hold and mono-theatre
    -- click. A cancelled gesture cannot roll into a different view shortcut.
    -- Reserve its stick and both triggers until all three are released.
    if s.view_chord or (not adjust and ((l3 and (p.bLeftTrigger>=30 or p.bRightTrigger>=30)) or
        (r3 and p.bLeftTrigger>=30 and p.bRightTrigger>=30))) then
        local lt,rt=p.bLeftTrigger,p.bRightTrigger
        if not s.view_chord then
            s.view_owner=r3 and "mono" or "left"
            s.view_chord=(b==B.R3 and lt>=180 and rt>=180 and mono_ready and has(rising,B.R3)) and "mono" or
                (b==B.L3 and lt>=30 and rt<30) and "portal" or
                (b==B.L3 and rt>=30 and lt<30) and "diorama" or
                (b==B.L3 and lt>=30 and rt>=30) and "screen" or "cancelled"
        end
        s.mono_ready=nil
        -- UObject/cursor reads stay on the engine tick; it validates queued
        -- requests against the current menu state before applying them.
        local menu=game_menu or enabled("WuWaControls_NativeMenu")
        local stick=s.view_owner=="mono" and B.R3 or B.L3
        if (has(b,stick) and b~=stick) or
            (s.view_chord~="screen" and s.view_chord~="mono" and lt>=30 and rt>=30) or
            (s.view_chord=="diorama" and menu) then
            s.view_chord="cancelled"; actions.view=nil
        end
        s.lone_l=false; s.lone_r=false; s.tap_l=-100; s.tap_r=-100; actions.freecam=nil
        s.rt=false; s.tap_rt=-100; s.turbo=false
        local trigger=s.view_chord=="portal" and lt or s.view_chord=="diorama" and rt or 0
        local fire=l3 and trigger>=180
        if s.view_chord=="mono" then fire=has(rising,B.R3) and lt>=180 and rt>=180 end
        if s.view_chord=="screen" then
            -- Continuous input is required: a polling pause, clock reversal,
            -- released modifier or trigger drop cannot finish an old long hold.
            local interrupted=not l3 or lt<30 or rt<30 or
                (s.view_hold_at and (lt<180 or rt<180)) or
                (s.view_sample_at and (t<s.view_sample_at or t-s.view_sample_at>0.25))
            if interrupted then s.view_chord="cancelled"; actions.view=nil
            else
                s.view_sample_at=t
                if lt>=180 and rt>=180 then
                    s.view_hold_at=s.view_hold_at or t
                    fire=t-s.view_hold_at>=0.8
                end
            end
        end
        if fire and not s.view_fired then
            actions.view={kind=s.view_chord,slot=index,at=t}; s.view_fired=true
        end
        state.Gamepad.wButtons=b & (~stick)
        state.Gamepad.bLeftTrigger=0; state.Gamepad.bRightTrigger=0
        input=nil; s.previous=b
        if (s.view_owner=="mono" and neutral(p)) or
            (s.view_owner~="mono" and not l3 and lt<30 and rt<30) then
            s.view_chord=nil; s.view_owner=nil; s.view_fired=false; s.view_hold_at=nil; s.view_sample_at=nil
        end
        return
    end
    if has(rising,B.L3) then s.lone_l=(b==B.L3) end
    if has(rising,B.R3) then s.lone_r=(b==B.R3) end
    if l3 and b~=B.L3 then s.lone_l=false end
    if r3 and b~=B.R3 then s.lone_r=false end
    local function tap(button,key,lone,action)
        if has(previous,button) and not has(b,button) and s[lone] and b==0 then
            if t-s[key]<=0.3 then actions[action]=true; s[key]=-100 else s[key]=t end
            s[lone]=false
        end
    end
    tap(B.L3,"tap_l","lone_l","screenshot"); tap(B.R3,"tap_r","lone_r","freecam")
    local consumed=0
    if l3 then
        for button,action in pairs({[B.A]="recenter",[B.B]="hud",[B.VIEW]="firstperson",[B.MENU]="sheet",[B.RB]="fixed"}) do
            if has(b,button) then consumed=consumed|B.L3|button; if has(rising,button) then actions[action]=true end end
        end
        if (mode==1 or mode==3) and not context_mouse then consumed=consumed|(b&(B.L3|B.Y|B.X)) end
        if mode==3 and not context_mouse and not adjust and has(b,B.DOWN) then
            consumed=consumed|B.L3|B.DOWN
            if (previous&(B.L3|B.DOWN))~=(B.L3|B.DOWN) then actions.motion=true end
        end
        -- Page browsing is opt-in to a visible sheet and uses the same rising-
        -- edge/focus rearm rules as other chords. Never steal normal game D-pad.
        if enabled("WuWaControls_ShowShortcutSheet") then
            local directions=b&(B.LEFT|B.RIGHT|B.UP)
            consumed=consumed|directions
            if directions~=0 then consumed=consumed|B.L3 end
            if directions==B.UP and has(rising,B.UP) then actions.sheet_auto=true
            elseif directions==B.LEFT and has(rising,B.LEFT) then actions.sheet_delta=-1
            elseif directions==B.RIGHT and has(rising,B.RIGHT) then actions.sheet_delta=1 end
        end
    end
    local force=utility_held(p)
    if mode==2 and not adjust and not context_mouse and not force then
        if p.bRightTrigger>=30 and not s.rt then
            s.turbo=t-s.tap_rt<=0.3; s.tap_rt=t
        end
        s.rt=p.bRightTrigger>=30; if not s.rt then s.turbo=false end
        input.turbo=s.turbo
        state.Gamepad.sThumbLX=0; state.Gamepad.sThumbLY=0; state.Gamepad.sThumbRX=0; state.Gamepad.sThumbRY=0
        state.Gamepad.bLeftTrigger=0; state.Gamepad.bRightTrigger=0
        consumed=consumed|B.LB
        local style=number("WuWaControls_FreeStyle",0,0,3)
        if style==2 or style==3 then consumed=consumed|B.RB end
        if style==3 and b==B.RB and has(rising,B.RB) then actions.acro_arm=true end
    elseif mode==1 and lb and not context_mouse and not adjust and not force then
        if p.bLeftTrigger>=30 or p.bRightTrigger>=30 then
            consumed=consumed|B.LB; state.Gamepad.bLeftTrigger=0; state.Gamepad.bRightTrigger=0
        end
    end
    if adjust then
        zero_pad(state.Gamepad); consumed=0xffff
    elseif (context_mouse and enabled("WuWaControls_AutoMouseMenus") or force) and enabled("WuWaControls_MouseAssist") then
        state.Gamepad.sThumbLX=0; state.Gamepad.sThumbLY=0
        consumed=consumed|B.A|B.B|B.X|B.UP|B.DOWN|B.LEFT|B.RIGHT
        if force then consumed=consumed|B.LB|B.R3 end
    elseif enabled("WuWaControls_PolarWalk") and mode~=2 and not context_mouse and not has(b,B.RB) then
        local length=math.sqrt(p.sThumbLX^2+p.sThumbLY^2)
        if length>12000 then state.Gamepad.sThumbLX=math.floor(p.sThumbLX/length*12000); state.Gamepad.sThumbLY=math.floor(p.sThumbLY/length*12000) end
    end
    state.Gamepad.wButtons=b & (~consumed)
    s.previous=b
end

cb.on_xinput_get_state(function(_,index,state,result)
    -- A real raw XInput poll makes the native HID snapshot unavailable before
    -- this callback. Ignore only VR-generated duplicate input while HID owns
    -- the shortcut route. Switching back requires a neutral sample.
    local ps=get("WuWaControls_PlayStationState") or ""
    if ps:sub(1,6)=="ps-v1," then return end
    if result==0 and state then choose_input_source("xinput") end
    process_pad(index,state,result)
end)

local function poll_playstation()
    local raw=get("WuWaControls_PlayStationState") or ""
    local generation,stamp,buttons,lt,rt,lx,ly,rx,ry=raw:match(
        "^ps%-v1,(%d+),(%d+),(%d+),(%d+),(%d+),(-?%d+),(-?%d+),(-?%d+),(-?%d+)$")
    local values={tonumber(buttons),tonumber(lt),tonumber(rt),tonumber(lx),tonumber(ly),tonumber(rx),tonumber(ry)}
    local valid_report=generation and tonumber(stamp) and now()*1000>=tonumber(stamp) and now()*1000-tonumber(stamp)<=250
    for i,v in ipairs(values) do
        local lo,hi=i==1 and 0 or i<=3 and 0 or -32768,i==1 and 65535 or i<=3 and 255 or 32767
        if v<lo or v>hi then valid_report=false end
    end
    if not valid_report then
        if input_source and input_source:sub(1,3)=="ps:" then
            input_source=nil; slots={}; actions={}; selected=nil; input=nil
        end
        return
    end
    choose_input_source("ps:"..generation)
    local pad={}; for i,k in ipairs(fields) do pad[k]=values[i] end
    -- This private copy is never returned to the game. Native HID input cannot
    -- consume the game's buttons; mapped XInput remains the preferred route.
    process_pad(4,{Gamepad=pad},0)
end

-- Camera-only acro dynamics. Sticks command body angular rates; releasing them
-- stops rotation without auto-levelling. No pawn/actor physics are modified.
local function qmul(a,b)
    return {w=a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        x=a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        y=a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        z=a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w}
end
local function qaxis(x,y,z,angle)
    local s=math.sin(angle/2)
    return {w=math.cos(angle/2),x=x*s,y=y*s,z=z*s}
end
local function attitude(rot)
    return qmul(qmul(qaxis(0,0,1,math.rad(rot.y)),qaxis(0,1,0,-math.rad(rot.x))),qaxis(1,0,0,-math.rad(rot.z)))
end
local function qrotate(q,v)
    local r=qmul(qmul(q,{w=0,x=v.x,y=v.y,z=v.z}),{w=q.w,x=-q.x,y=-q.y,z=-q.z})
    return {x=r.x,y=r.y,z=r.z}
end
local function qangles(q)
    local f=qrotate(q,{x=1,y=0,z=0}); local r=qrotate(q,{x=0,y=1,z=0}); local u=qrotate(q,{x=0,y=0,z=1})
    return {x=math.deg(math.asin(math.max(-1,math.min(1,f.z)))),y=math.deg(math.atan(f.y,f.x)),z=-math.deg(math.atan(r.z,u.z))}
end
local function qinverse(q) return {w=q.w,x=-q.x,y=-q.y,z=-q.z} end
local function qslerp(a,b,t)
    local dot=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z
    if dot<0 then b={w=-b.w,x=-b.x,y=-b.y,z=-b.z}; dot=-dot end
    local x,y=1-t,t
    if dot<0.9995 then
        local angle=math.acos(math.min(1,dot)); local s=math.sin(angle)
        x=math.sin((1-t)*angle)/s; y=math.sin(t*angle)/s
    end
    local q={w=a.w*x+b.w*y,x=a.x*x+b.x*y,y=a.y*x+b.y*y,z=a.z*x+b.z*y}
    local len=math.sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z)
    return {w=q.w/len,x=q.x/len,y=q.y/len,z=q.z/len}
end
local function movement_state(pawn)
    -- Reflected engine properties only; some WuWa rigs use a custom movement
    -- component, in which case stick handovers still work without this signal.
    local ok,key=pcall(function()
        if not has_function(pawn,"GetMovementComponent") then return nil end
        local component=pawn:GetMovementComponent()
        if not component or not component:get_class():find_property("MovementMode") then return nil end
        local mode_value=tonumber(component.MovementMode)
        if not mode_value then return nil end
        local custom=component:get_class():find_property("CustomMovementMode") and tonumber(component.CustomMovementMode) or 0
        return tostring(mode_value)..":"..tostring(custom or 0)
    end)
    return ok and key or nil
end
local function blend_look(info,rot,override,movement,game_rot)
    if not enabled("WuWaControls_SmoothFullFollow") then info.look_blend=nil; return rot end
    local t=now(); local target=attitude(rot); local state=info.look_blend
    if not state then state={override=override,movement=movement,output=target}; info.look_blend=state end
    if state.override~=override or (state.movement and movement and state.movement~=movement) then
        -- A finished game-view override uses the later stereo input directly.
        -- Its last displayed angle is now in game_rot, newer than the angle
        -- cached by this draw callback. Start the return from that actual view.
        local previous=state.override and not state.from and attitude(game_rot) or state.output
        state.override=override; state.start=t; state.from=previous
        state.duration=number("WuWaControls_FullFollowBlendTime",0.2,0.05,0.75)
    end
    state.movement=movement
    if state.from then
        local progress=math.max(0,math.min(1,(t-state.start)/state.duration))
        state.output=qslerp(state.from,target,progress*progress*(3-2*progress))
        if progress>=1 then state.from=nil end
    else state.output=target end
    -- Only handovers (stick or movement-state transitions) are blended. Do not low-pass ordinary aiming, animation
    -- or headset tracking, and never integrate twice for the two eyes.
    return state.from and qangles(state.output) or rot
end
local function rotator(r)
    local out={x=r.Pitch or r.pitch or r.x or r.X or 0,
        y=r.Yaw or r.yaw or r.y or r.Y or 0,z=r.Roll or r.roll or r.z or r.Z or 0}
    return valid(out) and out or nil
end
local function first_person_rotation(pawn,info,game_rot)
    local style=motion()
    local rot=point(game_rot)
    if style==1 or (style==0 and enabled("WuWaControls_LevelFirstPerson")) then rot.x=0; rot.z=0 end
    if style~=3 then return rot end
    -- Stock headset/controller aim consumes HMD yaw into the game camera and
    -- forces decoupled pitch. An independent actor-based view would then lose
    -- head turning or have its animation flattened. Leave the user's global
    -- settings intact and use the game view until that conflict is resolved.
    if number("VR_AimMethod",0,0,5)~=0 or enabled("VR_DecoupledPitch") then
        if info then
            info.look_blend=nil
            info.rotation_report="Full animation paused: select Game aim and turn Decoupled Pitch off. Using game view."
        end
        return rot,true
    end
    if not info or not info.bone or not has_function(info.mesh,"GetSocketRotation") then
        if info then info.rotation_report="Bone rotation unavailable; keeping game-camera rotation." end
        return rot,true
    end
    -- Bone axes differ between rigs. Calibrate their local orientation once;
    -- apply the animation delta to the character's world orientation. This
    -- includes combat body turns, not just neck motion. The animated-position
    -- preset is the separate choice for ordinary right-stick camera steering.
    local actor=has_function(pawn,"K2_GetActorRotation") and rotator(pawn:K2_GetActorRotation())
    if not actor then return rot,true end
    local actor_inverse=qinverse(attitude(actor))
    local function sample(bone)
        local r=rotator(info.mesh:GetSocketRotation(bone))
        return r and qmul(actor_inverse,attitude(r))
    end
    info.rotation_reference=info.rotation_reference or {}
    local reference=info.rotation_reference
    local head_hidden=bone_hidden(info.mesh,info.bone)
    local head=not head_hidden and sample(info.bone) or nil
    local parent=info.parent_anchor and sample(info.parent_anchor.bone) or nil
    if head and not reference.head then reference.head=head end
    if parent and not reference.parent then reference.parent=parent end
    local current,origin=head or parent,head and reference.head or reference.parent
    local follow_game=not (current and origin)
    if current and origin then
        rot=qangles(qmul(attitude(actor),qmul(current,qinverse(origin))))
        info.rotation_report=head and "Rotation: animated head delta." or "Rotation: visible-parent approximation."
    else
        info.rotation_report="Bone rotation unavailable; keeping game-camera rotation."
    end
    local look=number("WuWaControls_FullFollowLook",2,0,2)
    if look==1 then rot.x=game_rot.x end
    if look==2 then
        local override=now()<stick_view_until
        if override then rot=point(game_rot); follow_game=true end
        info.movement_state=movement_state(pawn)
        rot=blend_look(info,rot,override,info.movement_state,game_rot)
    else info.look_blend=nil end
    return rot,follow_game and not (info.look_blend and info.look_blend.from)
end
local function first_person_offset_yaw(pawn,game_yaw,view_yaw,follow_game)
    if follow_game then return math.rad(game_yaw) end
    -- In game-view override the actor may turn 180 degrees to walk backwards.
    -- Its eye offset must follow the rendered view (including the handover),
    -- otherwise the view looks forward from behind the animated head.
    if motion()==3 and number("WuWaControls_FullFollowLook",2,0,2)==2 then
        return math.rad(view_yaw or game_yaw)
    end
    -- Exact-animation view follows the body. Rotating its eye offsets with
    -- the independent third-person camera makes them orbit during strafing.
    -- Leave the accepted animated-position preset byte-for-byte equivalent.
    if motion()==3 and has_function(pawn,"K2_GetActorRotation") then
        local actor=rotator(pawn:K2_GetActorRotation())
        if actor then return math.rad(actor.y) end
    end
    return math.rad(game_yaw)
end
local function flight_axis(value)
    -- Continuous deadzone for rate control, unlike the original walk deadzone.
    local v=math.max(-1,math.min(1,value/32767)); local a=math.abs(v)
    return a<=0.08 and 0 or (v<0 and -1 or 1)*(a-0.08)/0.92
end
local function throttle(p)
    if number("WuWaControls_AcroThrottle",0,0,1)==1 then return math.max(0,math.min(1,(p.sThumbLY+32768)/65535)) end
    return p.bRightTrigger<6 and 0 or p.bRightTrigger/255
end
local function vector(p) return Vector3f.new(p.x,p.y,p.z) end
local trace_names={"WorldContextObject","Start","End","Radius","TraceChannel","bTraceComplex","ActorsToIgnore","DrawDebugType","OutHit","bIgnoreSelf","TraceColor","TraceHitColor","DrawTime","ReturnValue"}
local function trace_library()
    if collision_cache and alive(collision_cache.lib) then return collision_cache.lib end
    local klass=api:find_uobject("Class /Script/Engine.KismetSystemLibrary")
    local lib=klass and klass:get_class_default_object()
    local fn=lib and lib:get_class():find_function("SphereTraceSingle")
    assert(fn,"SphereTraceSingle is unavailable")
    -- Validate the reflected parameter order before invoking it; no raw object
    -- addresses, fixed offsets, guessed ABI, or spawned collision proxy actor.
    local field=fn:get_child_properties(); local count=0
    for _=1,32 do
        if not field then break end
        -- get_child_properties returns FField, whose Lua binding has no
        -- property flags method. Check the entire ordered chain, return last.
        count=count+1
        assert(field:get_fname():to_string()==trace_names[count],"Unsupported SphereTraceSingle parameter layout")
        field=field:get_next()
    end
    assert(count==#trace_names and not field,"Incomplete SphereTraceSingle contract")
    collision_cache={lib=lib}
    return lib
end
local function sweep_once(pawn,from,to)
    local out={}; local lib=trace_library()
    local color={R=0,G=0,B=0,A=0}
    local hit=lib:SphereTraceSingle(pawn,vector(from),vector(to),number("WuWaControls_CollisionRadius",10,1,50),
        0,enabled("WuWaControls_CollisionComplex"),{pawn},0,out,true,color,color,0)
    assert(type(hit)=="boolean","Unexpected sphere trace return type")
    if not hit then return {pos=to,hit=false} end
    local h=out.result
    assert(h,"Sphere trace did not return HitResult")
    local t=h.Time; local n=h.Normal and point(h.Normal)
    assert(type(t)=="number" and t==t and t>=0 and t<=1 and valid(n),"Invalid collision result")
    local len=math.sqrt(n.x*n.x+n.y*n.y+n.z*n.z)
    assert(len>0.5 and len<1.5,"Invalid collision normal")
    n={x=n.x/len,y=n.y/len,z=n.z/len}
    if h.bStartPenetrating then return {pos=from,hit=true,inside=true,normal=n} end
    local d={x=to.x-from.x,y=to.y-from.y,z=to.z-from.z}
    local distance=math.sqrt(d.x*d.x+d.y*d.y+d.z*d.z)
    local safe=math.max(0,t-0.2/math.max(distance,0.2))
    return {pos={x=from.x+d.x*safe,y=from.y+d.y*safe,z=from.z+d.z*safe},hit=true,normal=n}
end
local function sweep_camera(pawn,from,to)
    if not enabled("WuWaControls_FreeCollision") then collision_report=""; return to end
    local ok,result=pcall(function()
        local hit=sweep_once(pawn,from,to)
        if not hit.hit then collision_report="collision=clear"; return hit.pos end
        if hit.inside then free.velocity={x=0,y=0,z=0}; collision_report="collision=inside; move/reset with collision off"; return from end
        local n=hit.normal; local v=free.velocity
        local inward=v.x*n.x+v.y*n.y+v.z*n.z
        if inward<0 then for _,k in ipairs({"x","y","z"}) do v[k]=v[k]-inward*n[k] end end
        -- One extra sweep along the contact plane allows ground/wall sliding.
        -- Each physics substep is bounded to two sweeps, never an unbounded loop.
        local d={x=to.x-hit.pos.x,y=to.y-hit.pos.y,z=to.z-hit.pos.z}
        local into=d.x*n.x+d.y*n.y+d.z*n.z
        if into<0 then for _,k in ipairs({"x","y","z"}) do d[k]=d[k]-into*n[k] end end
        local slide={x=hit.pos.x+d.x,y=hit.pos.y+d.y,z=hit.pos.z+d.z}
        local second=sweep_once(pawn,hit.pos,slide)
        if second.hit then free.velocity={x=0,y=0,z=0} end
        collision_report="collision=blocked / sliding"
        return second.pos
    end)
    if ok then return result end
    free.velocity={x=0,y=0,z=0}; free.armed=false
    collision_report="collision unavailable; movement held"
    if collision_error~=tostring(result) then
        functions.log_warn("WuWaControls collision unavailable: "..tostring(result))
    end
    collision_error=tostring(result); collision_cache=nil
    return from
end
local function update_acro_view()
    -- Apply the same camera tilt while disarmed, paused, resetting or flying.
    -- It is attached to the drone body and does not tilt its thrust.
    free.rot=qangles(qmul(free.q,qaxis(0,1,0,-math.rad(number("WuWaControls_AcroTilt",15,0,60)))))
end
local function move_acro(p,delta,pawn)
    local throttle_mode=number("WuWaControls_AcroThrottle",0,0,1)
    if free.throttle_mode~=throttle_mode then free.throttle_mode=throttle_mode; free.armed=false end
    local stop=has(p.wButtons,B.LB)
    if stop then
        free.armed=false; free.velocity={x=0,y=0,z=0}
        if has(p.wButtons,B.RB) then free.q=attitude({x=0,y=qangles(free.q).y,z=0}) end
        return
    end
    if acro_arm_requested then
        if free.armed then free.armed=false
        elseif throttle(p)<=0.05 then free.armed=true; free.arm_denied=false
        else free.arm_denied=true end
    end
    if not free.armed then free.velocity={x=0,y=0,z=0}; return end
    local expo=number("WuWaControls_AcroExpo",0.35,0,0.9)
    local function rate(v,max_rate) local a=flight_axis(v); return math.rad(max_rate*((1-expo)*a+expo*a^3)) end
    local max_rate=number("WuWaControls_AcroRate",360,30,1000)
    local x=-rate(p.sThumbRX,max_rate)
    local y=rate(p.sThumbRY,max_rate)*(enabled("WuWaControls_AcroInvertPitch") and -1 or 1)
    local z=rate(p.sThumbLX,number("WuWaControls_AcroYawRate",180,30,720))
    local omega=math.sqrt(x*x+y*y+z*z)
    local steps=math.max(1,math.ceil(delta*240)); local dt=delta/steps
    local drag=number("WuWaControls_AcroDrag",0.2,0,3)
    local thrust=981*number("WuWaControls_AcroThrust",4,1.1,10)*throttle(p)
    for _=1,steps do
        if omega>0 then free.q=qmul(free.q,qaxis(x/omega,y/omega,z/omega,omega*dt)) end
        local norm=math.sqrt(free.q.w^2+free.q.x^2+free.q.y^2+free.q.z^2)
        for _,k in ipairs({"w","x","y","z"}) do free.q[k]=free.q[k]/norm end
        local up=qrotate(free.q,{x=0,y=0,z=1})
        local acceleration={x=up.x*thrust,y=up.y*thrust,z=up.z*thrust-981}
        local from=point(free.pos); local target={}
        local decay=math.exp(-drag*dt); local integral=drag>0.00001 and (1-decay)/drag or dt
        for _,k in ipairs({"x","y","z"}) do
            local v,a=free.velocity[k],acceleration[k]
            target[k]=from[k]+v*integral+a*(drag>0.00001 and (dt-integral)/drag or dt*dt/2)
            free.velocity[k]=math.max(-30000,math.min(30000,v*decay+a*integral))
        end
        free.pos=sweep_camera(pawn,from,target)
        if not free.armed then break end
    end
end
local function move_freecam(p,delta,pawn)
    local style=number("WuWaControls_FreeStyle",0,0,3)
    if not enabled("WuWaControls_FreeCollision") then collision_report="" end
    if free.style~=style then
        free.style=style; free.velocity={x=0,y=0,z=0}; free.bank=0; free.rot.z=0
        free.q=attitude(style==3 and {x=0,y=free.rot.y,z=0} or free.rot)
        free.armed=false; free.arm_denied=false; collision_report=""
    end
    -- A retained view may outlive focus or the physical pad. No inertial drift
    -- or cruise movement is allowed without a fresh, focused input sample.
    if not p or has(p.wButtons,B.L3) or has(p.wButtons,B.R3) or has(p.wButtons,B.MENU) then
        free.velocity={x=0,y=0,z=0}; free.armed=false
        if style==3 then update_acro_view() end
        return
    end
    if style==3 then move_acro(p,delta,pawn); update_acro_view(); return end
    local from=point(free.pos)
    local turn=number("WuWaControls_FreeTurn",90,10,300)
    local lx,ly,rx,ry=axis(p.sThumbLX),axis(p.sThumbLY),axis(p.sThumbRX),axis(p.sThumbRY)
    free.rot.x=math.max(-85,math.min(85,free.rot.x+ry*turn*delta))
    if style==2 then
        free.bank=((free.bank+rx*turn*delta+180)%360)-180
        free.rot.y=(free.rot.y+(lx+math.sin(math.rad(free.bank))*0.8)*turn*delta)%360
        free.rot.z=enabled("WuWaControls_FlightRoll") and free.bank or 0
        local speed=number("WuWaControls_PlaneSpeed",300,0,3000)
        local throttle=((p.bRightTrigger>=30 and p.bRightTrigger or 0)-(p.bLeftTrigger>=30 and p.bLeftTrigger or 0))/255
        if throttle~=0 then speed=math.max(0,math.min(3000,speed+throttle*300*delta)); set("WuWaControls_PlaneSpeed",speed) end
        if has(p.wButtons,B.LB) then return end -- brake without discarding position or cruise setting
        if has(p.wButtons,B.RB) then speed=speed*3 end
        local yaw,pitch=math.rad(free.rot.y),math.rad(free.rot.x)
        free.pos.x=free.pos.x+math.cos(yaw)*math.cos(pitch)*speed*delta
        free.pos.y=free.pos.y+math.sin(yaw)*math.cos(pitch)*speed*delta
        free.pos.z=free.pos.z+math.sin(pitch)*speed*delta
    else
        free.rot.y=(free.rot.y+rx*turn*delta)%360
        local speed=number("WuWaControls_FreeSpeed",600,10,3000)
        if p.bRightTrigger>=30 then speed=speed*(input.turbo and 9 or 3) end
        local yaw,pitch=math.rad(free.rot.y),math.rad(free.rot.x)
        local vertical=p.bLeftTrigger/255-(has(p.wButtons,B.LB) and 1 or 0)
        if style==0 then
            free.pos.x=free.pos.x+(math.cos(yaw)*math.cos(pitch)*ly-math.sin(yaw)*lx)*speed*delta
            free.pos.y=free.pos.y+(math.sin(yaw)*math.cos(pitch)*ly+math.cos(yaw)*lx)*speed*delta
            free.pos.z=free.pos.z+(math.sin(pitch)*ly+vertical)*speed*delta
        else
            local target={x=(math.cos(yaw)*ly-math.sin(yaw)*lx)*speed,
                y=(math.sin(yaw)*ly+math.cos(yaw)*lx)*speed,z=vertical*speed}
            local response=number("WuWaControls_DroneResponse",6,0.5,20)
            local decay=math.exp(-response*delta)
            -- Integrate exponential velocity exactly for a constant target, so
            -- inertia and distance do not depend on frame/polling rate.
            for _,k in ipairs({"x","y","z"}) do
                local previous=free.velocity[k]
                free.pos[k]=free.pos[k]+target[k]*delta+(previous-target[k])*(1-decay)/response
                free.velocity[k]=target[k]+(previous-target[k])*decay
            end
            free.bank=free.bank+(-lx*20-free.bank)*(1-decay)
            free.rot.z=enabled("WuWaControls_FlightRoll") and free.bank or 0
        end
    end
    free.pos=sweep_camera(pawn,from,free.pos)
end

local function tick(_,delta)
    poll_playstation()
    tick_sequence=tick_sequence+1
    local t=now(); delta=math.max(0,math.min(delta,0.05))
    -- Observe the real menu/cursor signal even when camera/input controls are
    -- disabled. Manual mouse adjustment and the utility chord are not menus.
    local pc=api:get_player_controller(0)
    game_menu=(pc~=nil and pc.bShowMouseCursor==true) or enabled("WuWaControls_NativeMenu")
    local controls_enabled=enabled("WuWaControls_Enabled")
    if not controls_enabled or not enabled("WuWaControls_MouseAssist") then set("WuWaControls_AdjustMode",false) end
    focused=controls_enabled and enabled("WuWaControls_Focused") and not functions.is_drawing_ui()
    camera_active=controls_enabled and (focused or enabled("WuWaControls_KeepCameraOnFocusLoss"))
    local p=input and t-input.at<=0.25 and input.pad or nil
    if not focused or enabled("VR_WuWaGamepadPassthrough") then
        -- Camera placement/visibility is independent of owning Windows input.
        -- Discard stale axes/chords and require a neutral sample on return.
        slots={}; actions={}; input=nil; selected=nil; p=nil
    end
    if not camera_active then
        frame=nil; free=nil; context_mouse=false
        if mesh_cache then mesh_cache.look_blend=nil end
        restore_mesh(); mesh_mode=-1
        status="Controls paused (disabled, unfocused or UEVR open); camera and owned visibility released."
        emit(nil,false); return
    end
    mode=number("WuWaControls_CameraMode",0,0,3)
    local pawn=api:get_local_pawn(0)
    context_mouse=game_menu
    if actions.adjust then set("WuWaControls_AdjustMode",not adjusting()); p=nil end
    local adjust=adjusting()
    if not context_mouse and not adjust then
        if actions.freecam then toggle_camera(2) end
        if actions.firstperson then toggle_camera(3) end
        if actions.fixed then choose_camera(mode==1 and 0 or 1) end
        if actions.motion and mode==3 then
            if motion()==3 then set("WuWaControls_FirstMotion",prior_motion)
            else prior_motion=motion(); set("WuWaControls_FirstMotion",3) end
        end
    end
    if actions.hud then set("VR_EnableGUI",not enabled("VR_EnableGUI")) end
    -- WindowMode detects the enabled edge and recenters the aperture.
    local view=actions.view
    if view and selected==view.slot and slots[view.slot] and t>=view.at and t-view.at<=0.25 and not adjust then
        if view.kind=="portal" then set("WindowMode_Enabled",not enabled("WindowMode_Enabled"))
        elseif view.kind=="diorama" and not game_menu then
            set("WuWaDiorama_Enabled",not enabled("WuWaDiorama_Enabled"))
        elseif view.kind=="screen" then
            -- The original screen remains stereoscopic. Mono theatre is an
            -- independent overlay setting, so exiting restores this choice.
            if get("WuWaControls_ToggleStereoScreen")=="effective-v1" then
                set("WuWaControls_ToggleStereoScreen","toggle")
            else set("VR_2DScreenMode",not enabled("VR_2DScreenMode")) end
        elseif view.kind=="mono" then
            if get("WuWaControls_ToggleMonoTheatre")=="effective-v1" then
                set("WuWaControls_ToggleMonoTheatre","toggle")
            else set("VR_MonoTheatreMode",not enabled("VR_MonoTheatreMode")) end
        end
    end
    if game_menu or adjust then
        for _,s in pairs(slots) do
            if s.view_chord and (adjust or s.view_chord=="diorama") then s.view_chord="cancelled" end
            if adjust then s.mono_ready=nil end
        end
    end
    if actions.sheet then set("WuWaControls_ShowShortcutSheet",not enabled("WuWaControls_ShowShortcutSheet")) end
    if actions.sheet_auto then
        set("WuWaControls_SheetPage",0)
    elseif actions.sheet_delta then
        local page=number("WuWaControls_SheetPage",0,0,4)
        if page==0 then page=(context_mouse or adjust) and 3 or mode~=0 and 2 or 1 end
        set("WuWaControls_SheetPage",1+(page-1+actions.sheet_delta)%4)
    end
    acro_arm_requested=actions.acro_arm==true
    local recenter,screenshot=actions.recenter,actions.screenshot; actions={}
    mode=number("WuWaControls_CameraMode",0,0,3)
    local want_late=late_update()
    local motion_style=motion()
    if focused and not context_mouse and not adjust and p and
        (axis(p.sThumbLX)~=0 or axis(p.sThumbLY)~=0 or axis(p.sThumbRX)~=0 or axis(p.sThumbRY)~=0) then
        stick_view_until=t+0.4
    end
    if camera_mode~=mode or (mode==3 and (want_late and not camera_late or camera_motion~=motion_style)) then
        camera_mode=mode; free=nil; restore_mesh(); mesh_mode=-1
        if mesh_cache then mesh_cache.anchor=nil; mesh_cache.parent_anchor=nil; mesh_cache.rotation_reference=nil; mesh_cache.look_blend=nil end
        late_anchor_report="awaiting draw"
        status=({"Game camera.","Fixed third person.","Freecam.","First person; character visibility follows the selected option."})[mode+1]
    end
    -- Turning late sampling OFF keeps the established calibration. A socket
    -- just unhidden here may still contain the previous collapsed transform.
    camera_late=want_late
    camera_motion=motion_style
    if pawn~=last_pawn then restore_mesh(); last_pawn=pawn; mesh_mode=-1; mesh_cache=nil; free=nil; collision_cache=nil end
    local force=utility_held(p)
    local menu=context_mouse or force
    if p and adjust and not has(p.wButtons,B.L3) then
        if p.bLeftTrigger>=30 or p.bRightTrigger>=30 then
            set("UI_Distance",math.max(0.5,math.min(10,number("UI_Distance",2,0.5,10)+(p.bRightTrigger-p.bLeftTrigger)/255*delta)))
        end
        if has(p.wButtons,B.LB)~=has(p.wButtons,B.RB) then
            set("UI_Y_Offset",math.max(-10,math.min(10,number("UI_Y_Offset",0,-10,10)+(has(p.wButtons,B.RB) and 1 or -1)*delta*0.5)))
        end
    elseif p and not menu and not adjust and (mode==1 or mode==3) then
        if has(p.wButtons,B.L3) and has(p.wButtons,B.Y)~=has(p.wButtons,B.X) then
            local key=mode==3 and "WuWaControls_FirstUp" or "WuWaControls_FixedHeight"
            local maximum=mode==3 and 100 or 400
            local speed=mode==3 and 15 or 60
            set(key,math.max(-100,math.min(maximum,number(key,mode==3 and 0 or 30,-100,maximum)+(has(p.wButtons,B.Y) and 1 or -1)*delta*speed)))
        end
        if mode==1 and has(p.wButtons,B.LB) and (p.bLeftTrigger>=30 or p.bRightTrigger>=30) then
            set("WuWaControls_FixedDistance",math.max(-100,math.min(1500,number("WuWaControls_FixedDistance",150,-100,1500)+(p.bLeftTrigger-p.bRightTrigger)/255*delta*120)))
        end
    end
    frame=nil
    if pawn and pc and not menu and mode~=0 and base then
        local pos=point(pawn:K2_GetActorLocation()); local rot={x=base.rot.x,y=base.rot.y,z=base.rot.z}
        if mode==2 then
            if not free then free={pos=point(base.pos),rot=rot} end
            move_freecam(not adjust and p or nil,delta,pawn)
            frame={pos=point(free.pos),rot=point(free.rot)}
        elseif valid(pos) then
            local yaw=math.rad(rot.y)
            if mode==1 then
                local distance=number("WuWaControls_FixedDistance",150,-100,1500)
                pos.x=pos.x-math.cos(yaw)*distance; pos.y=pos.y-math.sin(yaw)*distance
                pos.z=pos.z+number("WuWaControls_FixedHeight",30,-100,400)
            else
                pos=first_person_anchor(pawn,pos,nil,not camera_late)
                -- Late full-animation orientation is sampled once before draw.
                -- This provisional position is replaced there before either eye.
                if not camera_late or motion()~=3 then rot=first_person_rotation(pawn,mesh_cache,rot) end
                yaw=first_person_offset_yaw(pawn,base.rot.y,rot.y)
                local f=number("WuWaControls_FirstForward",5,-100,100); local r=number("WuWaControls_FirstRight",0,-100,100)
                pos.x=pos.x+math.cos(yaw)*f-math.sin(yaw)*r; pos.y=pos.y+math.sin(yaw)*f+math.cos(yaw)*r
                pos.z=pos.z+number("WuWaControls_FirstUp",0,-100,100)
                -- When late sampling is selected, preserve the unmodified game
                -- rotation for the draw callback; do not integrate animation twice.
            end
            frame={pos=pos,rot=rot,game_rot=point(base.rot)}
        end
    end
    if pawn and frame then
        update_mesh(pawn)
        if mode==1 then status="Fixed third person." elseif mode==2 then
            local style=number("WuWaControls_FreeStyle",0,0,3)
            status=({"Freecam: Polar fly.","Freecam: hover drone.","Freecam: plane FPV.","Freecam: acro drone."})[style+1]
            if style==3 then
                status=status..(free.armed and " ARMED." or free.arm_denied and " Lower throttle before arming with RB." or " Paused/disarmed; low throttle + RB arms.")
            end
            if collision_report~="" then status=status.." "..collision_report end
            if not p then status=status.." Movement paused (no focused, fresh controller sample)." end
        end
    else
        if free then free.armed=false; free.velocity={x=0,y=0,z=0} end
        if mesh_cache then mesh_cache.look_blend=nil end
        restore_mesh(); mesh_mode=-1
        status=menu and "Game menu: camera override paused and owned visibility restored."
            or mode==0 and "Game camera."
            or "Waiting for player/camera data; owned visibility restored."
    end
    if mode==3 and camera_late then status=status.."; late="..late_anchor_report end
    if adjust then status="HUD / mouse adjustment ON; L3 + LB exits. "..status end
    local was=context_mouse; context_mouse=menu
    emit(p,force,recenter,screenshot); context_mouse=was
end
cb.on_pre_engine_tick(function(engine,delta)
    local ok,err=pcall(tick,engine,delta)
    if not ok then
        frame=nil; actions={}; input=nil; game_menu=false; restore_mesh()
        status="Controls paused: "..tostring(err):sub(1,170)
        if status~=last_error then functions.log_error(status); last_error=status end
        emit(nil,false)
    end
end)
-- Close-up match. Dialogue and story shots use long lenses (about 20-40 degrees
-- across) that VR's wide view (about 90 per eye) cannot show: from the game
-- camera's own spot the subject looks several times smaller than in 2D. Per
-- shot, move the eye along the game camera's view toward its subject so the
-- subject keeps a share of its 2D size. The game's own focus distance names the
-- subject when it sets one; a trace along the view stops the eye short of
-- anything in between. Normal lenses (above 60 degrees, i.e. gameplay) are left
-- alone, as are the other camera modes and the screen views.
local lens={dolly=0,shot=0,report=""}
function lens.forward(r)
    local p,y=math.rad(r.x),math.rad(r.y)
    return {x=math.cos(p)*math.cos(y),y=math.cos(p)*math.sin(y),z=math.sin(p)}
end
function lens.publish(text)
    if text~=lens.report then lens.report=text; set("WuWaControls_LensStatus",text) end
end
function lens.focus(pcm)
    local ok,value=pcall(function()
        local pp=pcm.CameraCachePrivate.POV.PostProcessSettings
        if pp.bOverride_DepthOfFieldFocalDistance and type(pp.DepthOfFieldFocalDistance)=="number" then
            return pp.DepthOfFieldFocalDistance
        end
    end)
    if ok and type(value)=="number" and value>30 and value<5000 then return value end
end
function lens.blocked(context,pawn,from,dir)
    local ok,value=pcall(function()
        local lib=trace_library(); local out={}; local reach=5000
        local to={x=from.x+dir.x*reach,y=from.y+dir.y*reach,z=from.z+dir.z*reach}
        local color={R=0,G=0,B=0,A=0}
        local hit=lib:SphereTraceSingle(context,vector(from),vector(to),5,0,true,pawn and {pawn} or {},0,out,true,color,color,0)
        if hit~=true or not out.result then return nil end
        local t=out.result.Time
        if type(t)=="number" and t==t and t>=0 and t<=1 then return t*reach end
    end)
    if ok then return value end
end
function lens.update()
    local strength=number("WuWaControls_LensMatch",75,0,100)/100
    if strength<=0 then lens.dolly=0; lens.pos=nil; lens.publish("Off"); return end
    if mode~=0 or enabled("WuWaControls_EffectiveScreen") or enabled("WuWaControls_EffectiveMonoTheatre") then
        lens.dolly=0; lens.pos=nil; lens.publish("Paused: game camera in full VR only"); return
    end
    local pc=api:get_player_controller(0)
    local pcm=pc and pc.PlayerCameraManager
    if not pcm then lens.dolly=0; lens.pos=nil; return end
    local fov=pcm:GetFOVAngle()
    local pos,rot=point(pcm:GetCameraLocation()),rotator(pcm:GetCameraRotation())
    if type(fov)~="number" or fov~=fov or not valid(pos) or not rot then lens.dolly=0; lens.pos=nil; return end
    if fov>60 or fov<5 then
        lens.dolly=0; lens.pos=nil
        lens.publish(string.format("Waiting for a close-up (lens now %.0f degrees)",fov)); return
    end
    -- A cut is a jump of the game camera; within a shot its own moves carry on
    -- and the subject distance measured at the cut is kept, so nothing jitters.
    local cut=not lens.pos
    if not cut then
        local d=math.sqrt((pos.x-lens.pos.x)^2+(pos.y-lens.pos.y)^2+(pos.z-lens.pos.z)^2)
        local a=lens.forward(rot); local b=lens.forward(lens.rot)
        cut=d>50 or a.x*b.x+a.y*b.y+a.z*b.z<math.cos(math.rad(10))
    end
    lens.pos,lens.rot=pos,rot
    if cut then
        local pawn=api:get_local_pawn(0)
        lens.shot=lens.shot+1
        lens.subject_focus=lens.focus(pcm)
        lens.subject_trace=lens.blocked(pawn or pc,pawn,pos,lens.forward(rot))
        lens.subject=lens.subject_focus or lens.subject_trace
    end
    if not lens.subject then
        lens.dolly=0; lens.publish(string.format("Close-up at %.0f degrees: subject distance unknown, view unchanged",fov)); return
    end
    -- The subject keeps strength^1 of its 2D size: distance d becomes d*k^strength.
    local k=math.tan(math.rad(fov)/2)/math.tan(math.rad(90)/2)
    local d=lens.subject
    local dolly=math.max(0,d-math.max(60,d*k^strength))
    if lens.subject_trace then dolly=math.min(dolly,math.max(0,lens.subject_trace-60)) end
    lens.dolly=dolly
    local text=string.format("Close-up at %.0f degrees: subject %.1f m (%s), moved %.1f m nearer",
        fov,d/100,lens.subject_focus and "game focus" or "trace",dolly/100)
    if cut then
        functions.log_info(string.format("[WuWaLens] shot=%d fov=%.1f focus=%s trace=%s dolly=%.0f strength=%.2f",
            lens.shot,fov,tostring(lens.subject_focus and math.floor(lens.subject_focus) or "none"),
            tostring(lens.subject_trace and math.floor(lens.subject_trace) or "none"),dolly,strength))
    end
    lens.publish(text)
end
local function update_camera_before_draw()
    -- UGameViewportClient::Draw runs on the game thread after world simulation,
    -- before either eye. Calibrate BEFORE hiding a new head, then refresh its
    -- position here. Never integrate flight twice, replay input, or inspect
    -- UObjects in the stereo callback. Option changes are applied at the tick.
    if not camera_active or mode~=3 or not frame then late_anchor_report="inactive"; return end
    local ok,err=pcall(function()
        local pc=api:get_player_controller(0)
        if not pc or pc.bShowMouseCursor==true or enabled("WuWaControls_NativeMenu") then
            if mesh_cache then mesh_cache.look_blend=nil end
            frame=nil; restore_mesh(); mesh_mode=-1; late_anchor_report="menu"; return
        end
        if not camera_late or not late_update() then return end
        local pawn=api:get_local_pawn(0)
        local main,asset
        if pawn then main,asset=character_mesh(pawn) end
        if not pawn or pawn~=last_pawn or not matches_rig(mesh_cache,main,asset) or not alive(mesh_cache.mesh) then
            frame=nil; restore_mesh(); mesh_mode=-1; late_anchor_report="rig changed; waiting for next tick"; return
        end
        local pos=point(pawn:K2_GetActorLocation())
        if not valid(pos) then
            frame=nil; restore_mesh(); mesh_mode=-1; late_anchor_report="invalid pose"; return
        end
        pos=first_person_anchor(pawn,pos,mesh_cache)
        local rot,follow_game=first_person_rotation(pawn,mesh_cache,frame.game_rot)
        local yaw=first_person_offset_yaw(pawn,frame.game_rot.y,rot.y,follow_game)
        local f=number("WuWaControls_FirstForward",5,-100,100); local r=number("WuWaControls_FirstRight",0,-100,100)
        pos.x=pos.x+math.cos(yaw)*f-math.sin(yaw)*r
        pos.y=pos.y+math.sin(yaw)*f+math.cos(yaw)*r
        pos.z=pos.z+number("WuWaControls_FirstUp",0,-100,100)
        if valid(pos) then
            frame.pos=pos
            frame.rot=rot
            -- A completed game-view handover must use the current rotation
            -- arriving at the stereo callback. Cache the offset recipe too,
            -- otherwise the eye position would still follow last frame's yaw.
            frame.follow_game=follow_game and {yaw=yaw,forward=f,right=r} or nil
            late_anchor_report=mesh_cache.anchor and "before draw" or "waiting for head pose"
            update_mesh(pawn,true)
        end
    end)
    if not ok then
        frame=nil; restore_mesh(); mesh_mode=-1; late_anchor_report="unavailable"
        if last_error~=tostring(err) then functions.log_warn("WuWaControls late anchor: "..tostring(err)); last_error=tostring(err) end
    end
end
cb.on_pre_viewport_client_draw(function()
    update_camera_before_draw()
    -- The game camera is final for this frame here, so a cut is seen before either eye.
    local lens_ok,lens_err=pcall(lens.update)
    if not lens_ok then
        lens.dolly=0; lens.pos=nil; lens.publish("Unavailable: "..tostring(lens_err):sub(1,120))
    end
    if not enabled("WuWaControls_Recording") then return end
    local ok,err=pcall(function()
        local pawn=api:get_local_pawn(0)
        local pc=api:get_player_controller(0)
        local control_rotation
        if pc and has_function(pc,"GetControlRotation") then
            local sampled,r=pcall(function() return rotator(pc:GetControlRotation()) end)
            if sampled then control_rotation=r end
        end
        local sample={tick=tick_sequence,clock=now(),mode=mode,motion=motion(),
            stick_override=number("WuWaControls_FullFollowLook",2,0,2),mesh_mode=number("WuWaControls_MeshMode",4,0,4),
            smooth_handover=enabled("WuWaControls_SmoothFullFollow"),
            handover_active=mesh_cache and mesh_cache.look_blend and mesh_cache.look_blend.from~=nil or false,
            game_view_active=mode==3 and motion()==3 and number("WuWaControls_FullFollowLook",2,0,2)==2 and now()<stick_view_until,
            movement_state=mesh_cache and mesh_cache.movement_state or nil,
            control_rotation=control_rotation,
            camera_active=camera_active,focused=focused,game_menu=game_menu,uevr_menu=functions.is_drawing_ui(),
            camera=frame,game=base,slot=selected,input=input and input.pad or nil,input_age=input and now()-input.at or nil,
            pawn=pawn and point(pawn:K2_GetActorLocation()) or nil,
            actor_yaw=pawn and math.deg(pawn_yaw(pawn)) or nil,
            anchor=mesh_cache and mesh_cache.anchor_report or nil,status=status}
        functions.dispatch_custom_event("WuWaControls.Input.v1",json.dump_string({motion_only=true,motion=sample}))
    end)
    if not ok then functions.log_warn("WuWaControls recording sample unavailable: "..tostring(err)) end
end)
cb.on_pre_calculate_stereo_view_offset(function(_,_,_,position,rotation)
    -- Only plain cached numbers here. UObjects are inspected on the engine tick.
    local p,r=point(position),point(rotation)
    if valid(p) and valid(r) then base={pos=p,rot=r} end
    if camera_active and frame and valid(frame.pos) and valid(frame.rot) then
        position.x=frame.pos.x; position.y=frame.pos.y; position.z=frame.pos.z
        local current_game=frame.follow_game
        if current_game and valid(r) then
            -- Pure cached math only; do not alter frame or advance a handover
            -- here. Both eyes start from the same animated head anchor.
            local old,new=current_game.yaw,math.rad(r.y)
            local dc,ds=math.cos(new)-math.cos(old),math.sin(new)-math.sin(old)
            position.x=position.x+dc*current_game.forward-ds*current_game.right
            position.y=position.y+ds*current_game.forward+dc*current_game.right
        else
            rotation.x=frame.rot.x; rotation.y=frame.rot.y; rotation.z=frame.rot.z
        end
    elseif mode==0 and lens.dolly>0 and valid(r) then
        -- Close-up match: along this frame's game camera view, same for both eyes.
        local f=lens.forward(r)
        position.x=position.x+f.x*lens.dolly; position.y=position.y+f.y*lens.dolly; position.z=position.z+f.z*lens.dolly
    end
end)
cb.on_script_reset(function()
    set("WuWaControls_AdjustMode",false)
    game_menu=false
    if mesh_cache then mesh_cache.look_blend=nil end
    restore_mesh(); frame=nil; input=nil; slots={}; actions={}; focused=false; camera_active=false; camera_late=false
    status="Polar controls stopped; camera and owned visibility released."; emit(nil,false)
end)
