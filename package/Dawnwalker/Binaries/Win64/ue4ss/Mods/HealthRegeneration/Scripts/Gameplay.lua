-- MIT. Configure native effects once per player session, with no idle worker.
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
local E=dofile(directory..'NativeEffects.lua')
local settings=SaveLoadContext.settings or dofile(directory..'Config.lua').load(directory)
HealthRegenerationRememberSettings(settings)
SaveLoadDiagnostics.debugLogging=settings.debugLogging==1
local diagnostics=dofile(directory..'UE4SSCommonDiagnostics.lua').new({
    debugLogging=settings.debugLogging==1,prefix='[Health Regeneration] ',
    output=HealthRegenerationOutput,clock=os.clock,
})
local pawn,world=SaveLoadContext.pawn,SaveLoadContext.world
local pawnId,worldId=pawn:GetAddress(),world:GetAddress()
local asc,blood,ascClass,gameplay
local effects={}
local cursor,attempts,pending,finished=1,0,false,false
local segmentMode=settings.restoreVampireSegments==1 and settings.vampireRegenPercent>0
local function current()
    if not E.valid(pawn) or not E.valid(world) then return false end
    if pawn:GetAddress()~=pawnId or pawn:GetWorld():GetAddress()~=worldId then return false end
    local player=gameplay:GetPlayerPawn(world,0)
    return E.valid(player) and player:GetAddress()==pawnId and player:IsLocallyControlled()==true
end
local function cleanup(effect)
    Session.onClose(function() E.remove(asc,effect) end)
end
-- Pause is safe even on a lifecycle notification thread. Removal precedes
-- unhooking, so an inactive calculation cannot fall back to its native parent.
Session.onClose(function()
    if _HRNativeStop then
        local calls,zeros,errors,ms=_HRNativeStop()
        diagnostics.debug('Segment calculations: %d calls, %d zero, %d errors, %.3f ms total',calls,zeros,errors,ms)
    end
end)
local jobs={
    function()
        ascClass=StaticFindObject('/Script/GameplayAbilities.AbilitySystemComponent')
        gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics')
        assert(E.valid(ascClass) and E.valid(gameplay),'Player services unavailable')
    end,
    function()
        if not current() then return 'wait' end
        asc=pawn:GetComponentByClass(ascClass)
        local state=pawn.PlayerState
        blood=E.valid(state) and state.BloodBar or nil
        if not E.valid(asc) or (segmentMode and not E.valid(blood)) then return 'wait' end
    end,
}
for _,stem in ipairs({'GE_VampireSegmentGuardRate','GE_HealthRegenerationHumanRate','GE_PlayerHealthRegen',
    'GE_HealthRegenerationSegments','MMC_HealthRegenerationUnlock','MMC_HealthRegenerationHeal'}) do
    jobs[#jobs+1]=function()
        -- Load one small owned class per frame; unused segment assets stay unloaded.
        if stem:find('^MMC_') and not segmentMode then return end
        effects[stem]=E.class(stem)
        if not effects[stem] then return 'wait' end
    end
end
local function effect(name) return assert(effects[name],name) end
local function tagComponent(name)
    local cdo=effect(name).cdo
    assert(#cdo.GEComponents>=1 and #cdo.GEComponents<=2,'Unexpected effect components')
    local component=cdo.GEComponents[1]
    assert(E.valid(component),'Effect tag component unavailable')
    return component
end
jobs[#jobs+1]=function()
    local e=effect('GE_HealthRegenerationSegments');cleanup(e)
    E.remove(asc,e) -- Remove the zero-rate, non-periodic preload placeholder.
end
jobs[#jobs+1]=function()
    local e=effect('GE_VampireSegmentGuardRate');cleanup(e)
    E.rate(e.cdo,segmentMode and 0 or settings.vampireRegenPercent,'vampire-rate')
end
jobs[#jobs+1]=function()
    local e=effect('GE_HealthRegenerationHumanRate');cleanup(e)
    E.rate(e.cdo,settings.humanRegenPercent,'human-rate')
end
for _,name in ipairs({'GE_VampireSegmentGuardRate','GE_HealthRegenerationHumanRate','GE_PlayerHealthRegen'}) do
    jobs[#jobs+1]=function() E.combat(tagComponent(name),settings.combatRegen==1,name..'-tags') end
end
jobs[#jobs+1]=function()
    local human=effect('GE_PlayerHealthRegen')
    local cdo=human.cdo
    Session.onClose(function()
        if not E.valid(asc) or not E.valid(cdo) then return end
        -- Journals have restored the original tags. Refresh their native
        -- subscriptions too, while suppressing an application-time heal.
        local original=cdo.bExecutePeriodicEffectOnApplication
        assert(type(original)=='boolean','Human periodic policy unavailable during cleanup')
        cdo.bExecutePeriodicEffectOnApplication=false
        local ok,err=pcall(function() E.remove(asc,human);E.apply(asc,human) end)
        cdo.bExecutePeriodicEffectOnApplication=original
        assert(ok,err)
    end)
    E.combat(cdo,settings.combatRegen==1,'human-legacy-tags')
    Session.change('human-application-tick',function()
        if not E.valid(cdo) then return nil,false end
        local value=cdo.bExecutePeriodicEffectOnApplication
        assert(type(value)=='boolean','Human periodic policy unavailable')
        return value
    end,function(value)cdo.bExecutePeriodicEffectOnApplication=value;return true end,false)
end
jobs[#jobs+1]=function()
    if not segmentMode then return end
    assert(type(_HRNativeBind)=='function','Segment restoration requires the bundled DLL and Framecore 2b')
    local e=effect('GE_HealthRegenerationSegments')
    E.combat(tagComponent('GE_HealthRegenerationSegments'),settings.combatRegen==1,'segment-tags')
    assert(_HRNativeBind(blood:GetAddress(),effect('MMC_HealthRegenerationUnlock').cdo:GetAddress(),
        effect('MMC_HealthRegenerationHeal').cdo:GetAddress(),settings.vampireRegenPercent/100,settings.debugLogging==1)==true,
        'Segment calculation initialization failed')
    E.segmentParameters(e.cdo)
end
-- Refresh the native human timer so its active inhibition callbacks use the
-- requested combat rule. Application ticks are disabled before this refresh.
jobs[#jobs+1]=function() E.remove(asc,effect('GE_PlayerHealthRegen')) end
jobs[#jobs+1]=function() E.apply(asc,effect('GE_PlayerHealthRegen')) end
for _,name in ipairs({'GE_VampireSegmentGuardRate','GE_HealthRegenerationHumanRate','GE_HealthRegenerationSegments'}) do
    jobs[#jobs+1]=function() E.remove(asc,effects[name]) end
    jobs[#jobs+1]=function()
        local enabled=(name=='GE_VampireSegmentGuardRate' and not segmentMode and settings.vampireRegenPercent>0)
            or (name=='GE_HealthRegenerationHumanRate' and settings.humanRegenPercent>0)
            or (name=='GE_HealthRegenerationSegments' and segmentMode)
        if enabled then E.apply(asc,effect(name)) end
    end
end
local run
local function schedule(delay)
    if pending or finished then return end
    pending=true
    ExecuteInGameThreadWithDelay(delay,run)
end
local step=diagnostics.wrap('effectSetup',function()
    if cursor>2 then assert(current(),'Player changed during effect setup') end
    return jobs[cursor]()
end)
run=function()
    pending=false
    local ok,result=pcall(step)
    if not Session.active then return end -- A nested lifecycle event owns the next setup.
    diagnostics.count('setupSlices')
    if not ok then
        finished=true
        if _HRNativePause then _HRNativePause() end
        diagnostics.error('Settings application stopped: %s',tostring(result))
        diagnostics.flush(true)
        HealthRegenerationClose()
        return
    end
    if result=='wait' then
        attempts=attempts+1
        if attempts>=20 then
            finished=true;diagnostics.error('Player effects unavailable; settings will retry after the next save load or player restart')
            diagnostics.flush(true)
            HealthRegenerationNeedsRetry()
            return
        end
        schedule(250);return
    end
    cursor=cursor+1
    if cursor<=#jobs then schedule(16);return end
    finished=true
    if diagnostics.debugLogging then
        diagnostics.debug('Applied human %.2f%%, vampire %.2f%%, combat %s, segments %s',
            settings.humanRegenPercent,settings.vampireRegenPercent,tostring(settings.combatRegen==1),tostring(segmentMode))
    end
    diagnostics.flush(true)
end
schedule(16)
