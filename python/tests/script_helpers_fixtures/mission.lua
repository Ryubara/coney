-- A made-up level script for the helper reference tests: it calls the helpers of global.lua in our own fixture.
-- Our own source (Coney), never a game script; mission.s.luac is what a stock Lua 4.0.1 `luac -s` makes of it.
function Start()
    Wake(1, true)
    Wake(2)
    if Level > 3 then
        Menu.Open(4)
    end
    BindSchedule("Later", 5)
end

function Roll(list)
    for k, v in list do
        BindShow(v)
    end
    WakeAll(list)
end
