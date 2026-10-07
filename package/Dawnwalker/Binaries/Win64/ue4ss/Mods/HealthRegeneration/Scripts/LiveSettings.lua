-- MIT. Persistent menu subscription; no work is performed when this module loads.
local M={}
function M.new(directory, report)
    local Adapter=dofile(directory..'UE4SSDawnwalkerSettings.lua')
    local schema=dofile(directory..'SettingsSchema.lua')
    local live=Adapter.new({modId="oOCamilleOo_HealthRegeneration",schema=schema,report=report,
        ids={
        ["vampireRegenPercent"]="vampireRegenPercent",
        ["humanRegenPercent"]="humanRegenPercent",
        ["combatRegen"]="combatRegen",
        ["restoreVampireSegments"]="restoreVampireSegments",
        ["logLevel"]="logLevel"
        }})
    live.start(function(id,callback)
        return dofile(directory..'ModDmmApi.lua').subscribe(id,function(values,...)
            dofile(directory..'ModDiagnostics.lua').setLevel(values.logLevel)
            local ok,err=pcall(callback,values,...)
            if not ok and report then report('Settings callback failed: '..tostring(err)) end
        end)
    end)
    return live
end
return M
