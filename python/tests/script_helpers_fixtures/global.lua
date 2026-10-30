-- A made-up global.lua for the helper reference tests. Our own source (Coney), never a game script;
-- global.s.luac is what a stock Lua 4.0.1 `luac -s` makes of it. Bind* are the "natives".
true = 1
false = nil
Level = 0
Scratch = 5
Hidden = 1

function Count(list)
    local n = 0
    for k, v in list do
        n = n + 1
    end
    return n
end

function Wake(who, quick)
    BindWake(who)
    return Count({who})
end

function Boot()
    Level = BindLevel()
    Boot = nil
end
Boot()

function Later()
    BindSchedule("Wake", 10)
end

Menu = {}
function Menu.Open(page)
    BindShow(page)
    Wake(page)
end

-- A native and a helper called after a table loop, where the stack walk of lua4 loses track of the calls.
function WakeAll(list)
    for k, v in list do
        BindWake(v)
    end
    BindShow(Count(list))
end

BindRegister("Later")
Hidden = nil
