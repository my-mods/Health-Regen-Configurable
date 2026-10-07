-- MIT. Local level adapter; pinned shared diagnostics remains unchanged.
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
local Common=dofile(directory..'UE4SSCommonDiagnostics.lua')
local M={}
function M.setLevel(value)
    if type(value)=='number' and value%1==0 and value>=0 and value<=4 then ModDiagnosticLevel=value end
end
function M.new(options)
    local o=options or {};local output=o.output or print;local prefix=o.prefix or ''
    local copy={};for k,v in pairs(o) do copy[k]=v end
    copy.mutable=true;copy.debugLogging=(ModDiagnosticLevel or 2)==4
    copy.output=function(text) if (ModDiagnosticLevel or 2)>=4 then output('[DEBUG] '..text) end end
    local d=Common.new(copy)
    local enabled=d.setEnabled
    local function emit(severity,tag,message,...)
        if (ModDiagnosticLevel or 2)<severity then return end
        local text=tostring(message)
        if select('#',...)>0 then local ok,result=pcall(string.format,message,...);if ok then text=result end end
        output(prefix..'['..tag..'] '..text)
    end
    local function bind()
        d.log=function(...) emit(2,'WARN',...) end
        d.error=function(...) emit(1,'ERROR',...) end
        d.warning=d.log
        d.info=function(...) emit(3,'INFO',...) end
    end
    function d.setLevel(value) M.setLevel(value);enabled((ModDiagnosticLevel or 2)==4);bind() end
    function d.setEnabled(_) enabled((ModDiagnosticLevel or 2)==4);bind() end
    bind();return d
end
return M
