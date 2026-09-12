-- MIT. Menu rates are percentage points per second.
local rates={0,0.25,0.5,0.75,1,1.25,1.5,1.75,2,2.5,3,3.5,4,4.5,5}
return {
    {key='vampireRegenPercent',default=1.5,values=rates},
    {key='humanRegenPercent',default=1.5,values=rates},
    {key='combatRegen',default=0,values={0,1}},
    {key='restoreVampireSegments',default=0,values={0,1}},
    {key='debugLogging',default=0,values={0,1}},
}
