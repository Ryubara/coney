-- A made-up mission script for the index tests: natives, handlers and nested functions. Our own source (Coney),
-- never a game script; mission.s.luac is what a stock Lua 4.0.1 `luac -s` makes of it.
function OnEnter(who)
    HudShowText("enter", who)
end

function Update()
    if CountThings() > 2 then
        ScheduleFunc(1.5, "Finish")
    end
end

function Finish()
    MissionEnd(1)
end

Mission = {}
function Mission.Setup()
    SetMsgHandler("MSG_ENTER", "OnEnter")
    RegisterUpdate(Update)
    PadSetHandler(0, function(button) Finish() end)
    LocalHelper(3)
end

function LocalHelper(n)
    return n * 2
end
