-- MIT. Use the common store at the save-load configuration boundary; never poll.
local M={}
function M.load(directory)
    local Store=dofile(directory..'ModSettingsStore.lua')
    local schema=dofile(directory..'SettingsSchema.lua')
    local ready,issue=dofile(directory..'LoggingSettings.lua').prepare(directory,schema)
    assert(ready,issue)
    local values,err,path=Store.load(directory,schema,function() return {} end)
    assert(values,'Health Regen - Configurable settings rejected: '..tostring(err))
    ModDiagnosticLevel=values.logLevel
    return values,path
end
return M
