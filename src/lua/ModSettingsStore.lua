-- MIT. Local output boundary over the byte-identical pinned preference store.
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
local env=setmetatable({print=function(text)
    if (ModDiagnosticLevel or 2)>=2 then print('[WARN] '..tostring(text)..'\n') end
end},{__index=_G})
return assert(loadfile(directory..'SettingsStore.lua','t',env))()
