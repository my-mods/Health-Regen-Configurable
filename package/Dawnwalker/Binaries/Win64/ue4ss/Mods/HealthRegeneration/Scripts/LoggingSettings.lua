-- MIT. Level migration before the existing preference-generation upgrades.
local M={}
function M.prepare(directory,schema)
    local Store=dofile(directory..'ModSettingsStore.lua')
    local path=Store.path(directory)
    local text,err,code=Store.read(path)
    if not text then if code~=2 then return nil,err end;return true end
    local original=text
    text=dofile(directory..'ModLogLevels.lua').normalizeIniHeaders(text)
    local row={key='logLevel',values={0,1,2,3,4},default=2}
    local level,problem=Store.parse(text,{row})
    if not level and problem~='Missing setting: logLevel' then return nil,problem end
    if level then ModDiagnosticLevel=level.logLevel end
    local _,invalid=Store.parse(text,schema)
    if invalid and not invalid:match('^Missing setting:') then return nil,invalid end
    local old,oldError=Store.parse(text,{{key='debugLogging',values={0,1}}})
    if not level and not old and oldError~='Missing setting: debugLogging' then return nil,oldError end
    local value=level and level.logLevel or (old and old.debugLogging==1 and 4 or 2)
    ModDiagnosticLevel=value
    local newline=text:find('\r\n',1,true) and '\r\n' or '\n'
    local additions={}
    for _,row in ipairs(schema) do
        local found,why=Store.parse(text,{row})
        if not found then
            if why~='Missing setting: '..row.key then return nil,why end
            local v=row.default
            if row.key=='logLevel' then v=value end
            if row.key=='parryWindowPercent' then
                local prior,e=Store.parse(text,{{key='factor',min=0.1,max=50}})
                if prior then v=prior.factor*100 elseif e~='Missing setting: factor' then return nil,e end
            elseif row.key=='dodgeInvulnerabilityWindowPercent' then
                local prior,e=Store.parse(text,{{key='dodgeInvulnerabilityPercent',min=-75,max=5000}})
                if prior then v=math.min(5000,prior.dodgeInvulnerabilityPercent+100)
                elseif e~='Missing setting: dodgeInvulnerabilityPercent' then return nil,e end
            end
            additions[#additions+1]=row.key..' = '..tostring(v)
        end
    end
    if #additions==0 and text==original then return true end
    local addition=table.concat(additions,newline)
    local count=0
    local updated=text:gsub('[^\r\n]+',function(line)
        if line:match('^%s*%[Settings%]%s*$') then count=count+1;return #additions>0 and (line..newline..addition) or line end
        return line
    end)
    if count==0 then updated=text..newline..'[Settings]'..newline..addition..newline end
    if count>1 then return nil,'Duplicate Settings section' end
    local backup,tmp=path..'.before-log-levels',path..'.log-levels.tmp'
    for _,p in ipairs({backup,tmp}) do
        local present,e,c=Store.read(p)
        if present or c~=2 then return nil,'Recover existing logging migration: '..p end
    end
    local made,why=Store.create(tmp,updated);if not made then return nil,why end
    if Store.read(tmp)~=updated or Store.read(path)~=original then os.remove(tmp);return nil,'Settings changed during logging migration' end
    local moved,e=os.rename(path,backup);if not moved then os.remove(tmp);return nil,e end
    local installed,ie=os.rename(tmp,path)
    if not installed then
        local restored,re=os.rename(backup,path)
        if not restored then return nil,tostring(ie)..'; rollback failed: '..tostring(re)..'; recover '..backup..' and '..tmp end
        os.remove(tmp);return nil,ie
    end
    if Store.read(path)~=updated then return nil,'Logging migration readback failed; original retained in '..backup end
    return true
end
return M
