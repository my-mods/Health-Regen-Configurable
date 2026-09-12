-- MIT. All discovery and changes happen in bounded save-load setup slices.
local M={}
local COMBAT='Player.IsEffectivelyInCombat'
function M.valid(object) return object ~= nil and object:IsValid() == true end
local function names(container)
    local result={}
    local values=container.GameplayTags
    assert(#values<=64,'Unexpected effect tag count')
    for i=1,#values do
        local value=values[i].TagName:ToString()
        assert(value:match('^[%w_%.]+$'),'Unexpected effect tag')
        result[#result+1]=value
    end
    return result
end
local function contains(values,name)
    for _,value in ipairs(values) do if value==name then return true end end
    return false
end
local function serialize(values)
    local result={}
    for _,value in ipairs(values) do result[#result+1]='(TagName="'..value..'")' end
    return '('..table.concat(result,',')..')'
end
local function container(values)
    local parents,seen={},{}
    for _,value in ipairs(values) do
        local parent=value:match('^(.*)%.')
        while parent do
            if not seen[parent] then parents[#parents+1]=parent;seen[parent]=true end
            parent=parent:match('^(.*)%.')
        end
    end
    return '(GameplayTags='..serialize(values)..',ParentTags='..serialize(parents)..')'
end
function M.combat(owner,enabled,key)
    local property=owner:Reflection():GetProperty('OngoingTagRequirements')
    assert(M.valid(property),'Effect eligibility property unavailable')
    Session.change(key,function()
        if not M.valid(owner) then return nil,false end
        return contains(names(owner.OngoingTagRequirements.IgnoreTags),COMBAT)
    end,function(blocked)
        assert(M.valid(owner),'Effect eligibility owner unavailable')
        local current=owner.OngoingTagRequirements
        local required,ignored=names(current.RequireTags),names(current.IgnoreTags)
        local target={}
        for _,name in ipairs(ignored) do if name~=COMBAT then target[#target+1]=name end end
        if blocked then target[#target+1]=COMBAT end
        -- Other tags and native query fields are left intact. These owned assets
        -- use empty queries; explicitly preserve their require/ignore containers.
        local p=owner:Reflection():GetProperty('OngoingTagRequirements')
        assert(M.valid(p),'Effect eligibility property was replaced')
        p:ImportText('(RequireTags='..container(required)..',IgnoreTags='..container(target)..')',
            p:ContainerPtrToValuePtr(owner),0,owner)
        return true -- A native property restore yields to the next frame.
    end,not enabled)
end
function M.rate(owner,percent,key)
    local property=owner:Reflection():GetProperty('Modifiers')
    assert(M.valid(property),'Effect modifiers unavailable')
    local function value()
        assert(#owner.Modifiers==1,'Unexpected rate modifier count')
        return owner.Modifiers[1].ModifierMagnitude.AttributeBasedMagnitude.Coefficient
    end
    Session.change(key,function()
        if not M.valid(owner) then return nil,false end
        local number=tonumber(value().Value)
        assert(number,'Rate coefficient unavailable')
        return number
    end,function(number) value().Value=number;return true end,percent/100)
end
function M.class(stem)
    local path='/Game/_Dawnwalker/Player/Effects/'..stem..'.'..stem..'_C'
    local class=StaticFindObject(path)
    -- These classes are hard references of stock player effects. LoadAsset uses
    -- the AssetRegistry, which has no entry for our new cooked classes.
    if not M.valid(class) then return nil end
    local cdo=class:GetCDO()
    assert(M.valid(cdo),'Required class default unavailable: '..stem)
    return {class=class,cdo=cdo}
end
function M.segmentParameters(owner)
    assert(#owner.Modifiers==2,'Unexpected segment modifier count')
    Session.change('segment-period',function()
        if not M.valid(owner) then return nil,false end
        return assert(tonumber(owner.Period.Value))
    end,function(value) owner.Period.Value=value;return true end,1)
    for i=1,2 do
        Session.change('segment-coefficient-'..i,function()
            if not M.valid(owner) then return nil,false end
            return assert(tonumber(owner.Modifiers[i].ModifierMagnitude.CustomMagnitude.Coefficient.Value))
        end,function(value)
            owner.Modifiers[i].ModifierMagnitude.CustomMagnitude.Coefficient.Value=value
            return true
        end,1)
    end
end
function M.remove(asc,effect)
    if M.valid(asc) and effect and M.valid(effect.class) then
        local count=asc:RemoveActiveGameplayEffectBySourceEffect(effect.class,nil,-1)
        assert(type(count)=='number' and count>=0,'Could not remove owned effect')
    end
end
function M.apply(asc,effect)
    assert(M.valid(asc) and M.valid(effect.class),'Effect application context unavailable')
    -- The borrowed context and result array never survive this callback.
    local context=asc:MakeEffectContext()
    asc:BP_ApplyGameplayEffectToSelf(effect.class,1,context)
    local handles=asc:GetActiveEffects({EffectDefinition=effect.class})
    assert(#handles==1,'Effect did not produce exactly one active instance')
end
return M
