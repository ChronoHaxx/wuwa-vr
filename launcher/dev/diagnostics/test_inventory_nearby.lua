-- Offline mock: no UEVR, process access, output files, or game calls.
local path=assert(arg[1])
package.path=path:match("^(.*[/\\])").."?.lua;"..package.path
local original_clock,original_time=os.clock,os.time
local function copy(v)
    if type(v)~="table" then return v end
    local out={}; for k,x in pairs(v) do out[k]=copy(x) end; return out
end
local function run(mode)
    local tick,cpu,reads,writes=0,0,0,0
    os.clock=function() cpu=cpu+0.000001; return cpu end
    os.time=function() return 1900000000+math.floor(tick/2) end
    local hooks,files,logs={},{},{}
    local lookup={}; local next_addr=100
    local function fname(s) return {to_string=function()return s end} end
    local function fn(params,call)
        local child
        for i=#params,1,-1 do
            local next_field=child; local p=params[i]
            child={get_fname=function()return fname(p[1])end,
                get_class=function()return {get_fname=function()return fname(p[2])end}end,
                get_next=function()return next_field end}
        end
        return setmetatable({get_child_properties=function()return child end},{__call=function(_,...)return call(...)end})
    end
    local function class(name,props,methods)
        next_addr=next_addr+1; local addr=next_addr
        return {get_address=function()return addr end,get_full_name=function()return name end,
            find_property=function(_,n)local k=props[n]; if k then return {get_class=function()return {get_fname=function()return fname(k)end}end}end end,
            find_function=function(_,n)return methods[n]end,get_class_default_object=function()return nil end}
    end
    local function obj(name,cls,outer,data)
        next_addr=next_addr+1;local a=next_addr;data=data or {}
        data.get_address=function()return a end;data.get_full_name=function()return name end
        data.get_class=function()return cls end;data.get_outer=function()return outer end
        data.is_a=function(_,c)return cls==c or data.static_child==c end
        local o=setmetatable({}, {__index=data,__newindex=function()writes=writes+1;error('mutation')end})
        lookup[a]=o;return o
    end
    local rootclass=class('World',{},{});local world=obj('World Current',rootclass,nil)
    local oldworld=obj('World Old',rootclass,nil)
    local assetclass=class('StaticMesh',{},{});local asset=obj('StaticMesh /Game/Test/Tree.Tree',assetclass,nil)
    local mat=obj('MaterialInstanceConstant /Game/Test/Leaf.Leaf',class('Material',{},{}),nil)
    local building=obj('StaticMesh /Game/Test/Building.Building',assetclass,nil)
    local stone=obj('MaterialInstanceConstant /Game/Test/Stone.Stone',class('Material',{},{}),nil)
    local pawnclass=class('Character',{}, {K2_GetActorLocation=fn({{'ReturnValue','StructProperty'}},function()return {x=0,y=0,z=0}end)})
    local pawn=obj('Pawn Current',pawnclass,world)
    local current_pawn=pawn
    local m={};local meshclass=class('StaticMeshComponent',{StaticMesh='ObjectProperty'},m)
    local comp
    m.GetOwner=fn({{'ReturnValue','ObjectProperty'}},function()return pawn end)
    m.K2_GetComponentLocation=fn({{'ReturnValue','StructProperty'}},function(o)return o.pos end)
    m.GetNumMaterials=fn({{'ReturnValue','IntProperty'}},function()return mode=='material_limit' and 100 or 1 end)
    local material_reads=0
    m.GetMaterial=fn({{'ElementIndex','IntProperty'},{'ReturnValue','ObjectProperty'}},function(o,i)material_reads=material_reads+1;assert(i<16);return o.material or mat end)
    local im={};for k,v in pairs(m) do im[k]=v end
    im.GetInstanceCount=fn({{'ReturnValue','IntProperty'}},function()return mode=='instance_limit' and 6000 or 3 end)
    im.GetInstanceTransform=fn({{'InstanceIndex','IntProperty'},{'OutInstanceTransform','StructProperty'},
        {'bWorldSpace','BoolProperty'},{'ReturnValue','BoolProperty'}},function(_,i,out,ws)
        assert(ws==true);out.result={Translation={X=i==1 and 99999 or 100,Y=0,Z=0},Rotation={X=0,Y=0,Z=0,W=1},Scale3D={X=1,Y=1,Z=1}};return true
    end)
    local instanceclass=class('HierarchicalInstancedStaticMeshComponent',{StaticMesh='ObjectProperty'},im)
    comp=obj('HISM Tree',instanceclass,world,{StaticMesh=asset,static_child=meshclass,pos={x=100,y=0,z=0}})
    local far=obj('StaticMesh Far',meshclass,world,{StaticMesh=asset,pos={x=999999,y=0,z=0}})
    local unrelated=obj('StaticMesh OtherWorld',meshclass,oldworld,{StaticMesh=asset,pos={x=100,y=0,z=0}})
    local bspec={{'Component','ObjectProperty'},{'Origin','StructProperty'},{'BoxExtent','StructProperty'},{'SphereRadius','FloatProperty'}}
    if mode=='bad_signature' then bspec[1][1]='WrongComponent' end
    local bounds_calls=0
    local libclass=class('KismetSystemLibrary',{}, {GetComponentBounds=fn(bspec,function(_,c,a,b,r)
        bounds_calls=bounds_calls+1;a.result=c.pos;b.result={x=20,y=30,z=100};r.result=110 end)})
    local lib=obj('Kismet Default',libclass,nil);libclass.get_class_default_object=function()return lib end
    local viewportclass=class('Viewport',{World='ObjectProperty'},{});local vdata={World=world}
    local viewport=obj('Viewport',viewportclass,nil,vdata)
    local engine=obj('Engine',class('Engine',{GameViewport='ObjectProperty'},{}),nil,{GameViewport=viewport})
    local objects={pawn,comp,far,unrelated}
    if mode=='many_components' then for i=1,520 do objects[#objects+1]=obj('Near'..i,meshclass,world,{StaticMesh=asset,pos={x=i,y=0,z=0}})end end
    if mode=='category_priority' or mode=='category_all' then
        objects={}
        for i=1,40 do objects[#objects+1]=obj('Building'..i,meshclass,world,
            {StaticMesh=building,material=stone,pos={x=i,y=0,z=0}})end
        objects[#objects+1]=comp
    end
    local long=mode=='long_scan' or mode=='world_change' or mode=='pawn_change' or mode=='reset'
    if long then objects={} end
    local array={get_object_count=function()return long and 3000000 or #objects end,
        get_object=function(_,i)reads=reads+1;return objects[i+1]end}
    json={dump_string=function(v,indent)
            assert(indent==-1,'payload size must use the encoding actually written')
            if mode=='save_error' then error('fixture encoder failure')end
            return string.rep('x',mode=='payload_limit' and (#v.components*2100000+1024)or 1024)
        end,
        dump_file=function(p,v,indent)assert(indent==-1);files[p]=copy(v);return true end,
        load_file=function(p)return files[p]end}
    uevr={api={to_uobject=function(_,a)return lookup[a]end,get_local_pawn=function()return current_pawn end,
        get_engine=function()return engine end,get_uobject_array=function()return array end,
        find_uobject=function(_,n)if n:find('StaticMeshComponent')then return meshclass elseif n:find('KismetSystemLibrary')then return libclass end end},
        sdk={callbacks={on_pre_engine_tick=function(f)hooks.tick=f end,on_script_reset=function(f)hooks.reset=f end}},
        params={functions={log_info=function(s)logs[#logs+1]=s end}}}
    package.loaded.wuwa_nearby_inventory=nil
    local inventory=require('wuwa_nearby_inventory')
    assert(not inventory.busy and hooks.tick==nil,'loading module armed a scan')
    local completion
    if mode=='category_all' or mode=='save_error' then
        assert(inventory.start({category=mode=='category_all'and 'all'or 'foliage',finished=function(r)completion=r end}))
    else assert(loadfile(path))()end
    assert(inventory.busy)
    assert(not inventory.start(),'concurrent scan accepted')
    for i=1,50 do
        tick=i
        if mode=='world_change' and i==2 then vdata.World=oldworld end
        if mode=='pawn_change' and i==2 then current_pawn=obj('Replacement Pawn',pawnclass,world)end
        hooks.tick()
        if mode=='reset' and i==1 then hooks.reset()end
    end
    local recorded_reads=reads
    for _=1,5 do hooks.tick()end
    assert(reads==recorded_reads,'finished callback still reads game state')
    assert(writes==0,'mutated UObject data')
    assert(not inventory.busy,'finished scan retained the busy lease')
    local n,r=0
    for p,v in pairs(files)do assert(p:match('^diagnostics/nearby%-static%-meshes%-[%d%-]+%.json$'));n=n+1;r=v end
    if mode=='save_error' then
        assert(n==0 and completion and completion.saved==false and completion.save_error)
        print('PASS '..mode);return
    end
    assert(n==1,'expected exactly one diagnostic output')
    if mode=='bad_signature' then assert(r.reason=='error' and bounds_calls==0)
    elseif mode=='long_scan' then assert(r.reason=='time_limit' and r.partial and reads<=4096*40)
    elseif mode=='world_change' or mode=='pawn_change' then assert(r.reason=='world_or_pawn_changed')
    elseif mode=='reset' then assert(r.reason=='script_reset')
    elseif mode=='many_components' then assert(r.reason=='component_limit' and r.component_count==512)
    elseif mode=='payload_limit' then assert(r.reason=='payload_limit' and r.partial and r.component_count==0)
    elseif mode=='category_priority' then
        assert(r.component_count==33 and r.skipped_other_components==8 and r.other_components==32)
        assert(r.components[33].foliage_name_hint and r.components[33].name=='HISM Tree')
    elseif mode=='category_all' then assert(r.component_count==41 and r.skipped_other_components==0)
    else
        assert(r.component_count==1,'unexpected component count '..tostring(r.component_count)..' '..tostring(r.reason)..' '..table.concat(r.errors,'; '))
        local c=r.components[1];assert(c.static_mesh.name:find('/Game/Test/Tree'))
        assert(c.bounds.origin.x==100 and c.bounds.extent.z==100)
        if mode=='material_limit'then assert(c.materials_partial and material_reads==16)end
        if mode=='instance_limit'then assert(c.instances_partial and r.retained_instances==256)
        else assert(c.instance_count==3 and #c.instances==2 and not c.instances_partial)end
        assert(c.instances[1].world_location.x==100 and c.instances[2].index==2)
    end
    print('PASS '..mode)
end
for _,mode in ipairs({'normal','bad_signature','material_limit','instance_limit','many_components','category_priority',
    'category_all','world_change','pawn_change','long_scan','reset','payload_limit','save_error'})do run(mode)end
os.clock,os.time=original_clock,original_time
