-- MIT. Main Menu > Mod Settings > Apply, then load a save.
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
function HealthRegenerationOutput(message) print(message..'\n') end
local Diagnostics=dofile(directory..'UE4SSCommonDiagnostics.lua')
HealthRegenerationReport=Diagnostics.new({prefix='[Health Regen - Configurable] ',output=HealthRegenerationOutput}).log
SaveLoadDiagnostics={debugLogging=false}
local report=HealthRegenerationReport
-- Match the common save-load contract: settings I/O belongs to Gameplay.lua.
local settingsSnapshot
function HealthRegenerationRememberSettings(values) settingsSnapshot=values end
local loading,loadComplete,completionSerial=false,false,0
function HealthRegenerationCanApply() return loadComplete and not loading end
local manager=dofile(directory..'UE4SSCommonSession.lua').new(_G,directory,report,{canCleanup=HealthRegenerationCanApply})
local latest,restartPending,restartController,restartPawn,retry
local engine,gameplay
local activationPending,activationContext,activationSerial=nil,nil,0
local function valid(object) return object~=nil and object:IsValid()==true end
local function pause()
    activationSerial=activationSerial+1
    if activationPending then CancelDelayedAction(activationPending);activationPending=nil end
    activationContext=nil
    retry=true
    if _HRNativePause then _HRNativePause() end
    manager.pause()
end
function HealthRegenerationNeedsRetry() retry=true end
function HealthRegenerationClose() retry=true;pause();manager.close() end
local session={pause=pause}
function session.open(file,context)
    if not HealthRegenerationCanApply() then return end
    -- A pending accepted save takes precedence over an owner-only rebind.
    if activationPending and activationContext and activationContext.settings==nil then context.settings=nil end
    activationContext=context
    latest={pawn=context.pawn:GetAddress(),world=context.world:GetAddress()}
    if activationPending then return end
    if _HRNativePause then _HRNativePause() end
    manager.pause()
    activationSerial=activationSerial+1
    local ticket=activationSerial
    activationPending=ExecuteInGameThreadWithDelay(16,function()
        if ticket~=activationSerial then return end
        activationPending=nil
        local requested=activationContext;activationContext=nil
        if not requested or not HealthRegenerationCanApply() then return end
        if not valid(requested.pawn) or not valid(requested.world) then return end
        latest={pawn=requested.pawn:GetAddress(),world=requested.world:GetAddress()}
        retry=false
        manager.open(file,requested)
    end)
end
-- The shared save adapter owns accepted save requests. Extend ClientRestart
-- for verified owner replacement without rereading settings on duplicate events.
local api=setmetatable({}, {__index=_G})
local function restart()
    if restartPending or not HealthRegenerationCanApply() then return end
    restartPending=true
    ExecuteInGameThreadWithDelay(16,function()
        restartPending=false
        if not HealthRegenerationCanApply() then return end -- Completion retries the retained event.
        local controller,player=restartController,restartPawn
        restartController,restartPawn=nil,nil
        if not latest then return end
        local resume=player==nil and retry
        local world
        if not resume then
            if not valid(controller) or not valid(player) then return end
            if controller:IsLocalController()~=true or player:IsLocallyControlled()~=true then return end
            local owned=controller.Pawn;world=controller:GetWorld()
            if not valid(world) or not valid(owned) or owned:GetAddress()~=player:GetAddress() then return end
            if player:GetWorld():GetAddress()~=world:GetAddress() or not valid(player.CharDevAttributeSet) then return end
            if not retry and latest.pawn==player:GetAddress() and latest.world==world:GetAddress() then return end
        end
        if not valid(engine) then engine=FindFirstOf('Engine') end
        if not valid(gameplay) then gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics') end
        if not valid(engine) or not valid(gameplay) or not valid(engine.GameViewport) then return end
        local activeWorld=engine.GameViewport:GetWorld()
        if not valid(activeWorld) or (world and activeWorld:GetAddress()~=world:GetAddress()) then return end
        local activePlayer=gameplay:GetPlayerPawn(activeWorld,0)
        if not valid(activePlayer) or activePlayer:IsLocallyControlled()~=true then return end
        if resume then player,world=activePlayer,activeWorld end
        if activePlayer:GetAddress()~=player:GetAddress() or player:GetWorld():GetAddress()~=world:GetAddress()
            or not valid(player.CharDevAttributeSet) then return end
        session.open(directory..'Gameplay.lua',{pawn=player,world=world,settings=settingsSnapshot})
    end)
end
function api.RegisterHook(path,pre,post)
    if path=='/Script/Engine.PlayerController:ClientRestart' then
        return RegisterHook(path,pre,function(context,pawn)
            post(context,pawn)
            restartController,restartPawn=context:get(),pawn:get()
            restart()
        end)
    elseif path=='/Script/DogwoodCombat.CombatSubsystem:OnLoadingScreenStateChanged' then
        return RegisterHook(path,pre,function(context,state)
            local value=tonumber(state:get())
            if value==0 then
                loading,loadComplete=false,true;completionSerial=completionSerial+1
                manager.resumeCleanup()
            elseif value and value>=1 and value<=4 then
                loading,loadComplete=true,false
                pause()
            end
            post(context,state)
            if value==0 and (restartPawn or retry) then restart() end
        end)
    elseif path=='/Script/DogwoodUI.SaveWindowBase:RequestLoadSave' then
        return RegisterHook(path,function(...)
            loadComplete=false;pause();return pre(...)
        end,post)
    elseif path=='/Script/Persistency.SaveSystemBlueprintFunctionLibrary:LoadLastSave'
        or path=='/Script/Persistency.SaveSystemBlueprintFunctionLibrary:TryQuickload' then
        local requests={}
        return RegisterHook(path,function(...)
            requests[#requests+1]=completionSerial
            return pre(...)
        end,function(context,result)
            local before=table.remove(requests)
            local accepted=result
            if type(accepted)~='boolean' then accepted=result and result:get() end
            if accepted==true and before==completionSerial then loadComplete=false;pause() end
            return post(context,result)
        end)
    end
    return RegisterHook(path,pre,post)
end
local started,err=pcall(function()
    dofile(directory..'UE4SSDawnwalkerSaveLoad.lua').start(api,session,directory..'Gameplay.lua',report,SaveLoadDiagnostics,{requireLoadComplete=true})
end)
if not started then report('Save-load hooks unavailable: '..tostring(err)) end
