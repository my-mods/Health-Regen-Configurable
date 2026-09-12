-- MIT. Use the common store at the save-load configuration boundary; never poll.
local M={}
function M.load(directory)
    local Store=dofile(directory..'SettingsStore.lua')
    local schema=dofile(directory..'SettingsSchema.lua')
    local values,err,path=Store.load(directory,schema,function() return {} end)
    assert(values,'Health Regeneration settings rejected: '..tostring(err))
    return values,path
end
return M
