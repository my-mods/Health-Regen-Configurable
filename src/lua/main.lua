-- MIT. Main Menu > Mod Settings > Apply, then load a save.
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
function HealthRegenerationOutput(message) print(message..'\n') end
local Diagnostics=dofile(directory..'UE4SSCommonDiagnostics.lua')
HealthRegenerationReport=Diagnostics.new({prefix='[Health Regeneration] ',output=HealthRegenerationOutput}).log
SaveLoadDiagnostics={debugLogging=false}
local report=HealthRegenerationReport
-- Match the common save-load contract: settings I/O belongs to Gameplay.lua.
local settingsSnapshot
function HealthRegenerationRememberSettings(values) settingsSnapshot=values end
local manager=dofile(directory..'UE4SSCommonSession.lua').new(_G,directory,report)
local latest,loading,restartPending,restartController,restartPawn,retry
local engine,gameplay
local activationPending,activationContext
local function valid(object) return object~=nil and object:IsValid()==true end
local function pause()
    if _HRNativePause then _HRNativePause() end
    manager.pause()
end
function HealthRegenerationNeedsRetry() retry=true end
function HealthRegenerationClose() retry=true;pause();manager.close() end
local session={pause=pause}
function session.open(file,context)
    -- A pending accepted save takes precedence over an owner-only rebind.
    if activationPending and activationContext and activationContext.settings==nil then context.settings=nil end
    activationContext=context
    if activationPending then return end
    if _HRNativePause then _HRNativePause() end
    manager.pause()
    activationPending=ExecuteInGameThreadWithDelay(16,function()
        activationPending=nil
        local requested=activationContext;activationContext=nil
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
    if restartPending then return end
    restartPending=true
    ExecuteInGameThreadWithDelay(16,function()
        restartPending=false
        if loading then return end -- Completion retries the retained event.
        local controller,player=restartController,restartPawn
        restartController,restartPawn=nil,nil
        if not latest or not valid(controller) or not valid(player) then return end
        if controller:IsLocalController()~=true or player:IsLocallyControlled()~=true then return end
        local world,owned=controller:GetWorld(),controller.Pawn
        if not valid(world) or not valid(owned) or owned:GetAddress()~=player:GetAddress() then return end
        if player:GetWorld():GetAddress()~=world:GetAddress() or not valid(player.CharDevAttributeSet) then return end
        if not retry and latest.pawn==player:GetAddress() and latest.world==world:GetAddress() then return end
        if not valid(engine) then engine=FindFirstOf('Engine') end
        if not valid(gameplay) then gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics') end
        if not valid(engine) or not valid(gameplay) or not valid(engine.GameViewport) then return end
        local activeWorld=engine.GameViewport:GetWorld()
        if not valid(activeWorld) or activeWorld:GetAddress()~=world:GetAddress() then return end
        local activePlayer=gameplay:GetPlayerPawn(activeWorld,0)
        if not valid(activePlayer) or activePlayer:GetAddress()~=player:GetAddress() then return end
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
            loading=value and value>=1 and value<=4
            post(context,state)
            if not loading and restartPawn then restart() end
        end)
    end
    return RegisterHook(path,pre,post)
end
local started,err=pcall(function()
    dofile(directory..'UE4SSDawnwalkerSaveLoad.lua').start(api,session,directory..'Gameplay.lua',report,SaveLoadDiagnostics)
end)
if not started then report('Save-load hooks unavailable: '..tostring(err)) end
