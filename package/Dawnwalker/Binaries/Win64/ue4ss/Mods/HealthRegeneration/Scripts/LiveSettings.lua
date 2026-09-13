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
        ["debugLogging"]="debugLogging"
        }})
    live.start(function(id,callback)
        return dofile(directory..'dmm_api.lua').subscribe(id,callback)
    end)
    return live
end
return M
