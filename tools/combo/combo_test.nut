
function CB_Start()
{
    local p=GetPlayerArray()[0]
    local base=Vector(2560,468,264+8)
    local best=0.0; local yaw=0.0
    for(local a=0;a<360;a+=15)
    {
        local f=Vector(0,a,0).AnglesToForward()
        local t=TraceHull(base,base+f*900,p.GetBoundingMins(),p.GetBoundingMaxs(),p,0x0201400B,8)
        local d=TraceHull(base+f*500,base+f*500+Vector(0,0,-64),p.GetBoundingMins(),p.GetBoundingMaxs(),p,0x0201400B,8)
        if(!t.startSolid && d.fraction<1.0 && t.fraction>best) { best=t.fraction; yaw=a }
    }
    p.SetOrigin(base); p.SetVelocity(Vector(0,0,0)); p.SnapEyeAngles(Vector(0,yaw,0))
    printl("CB_START yaw="+yaw+" clear="+best)
}
