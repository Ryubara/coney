-- A second made-up level script for the helper reference tests. Our own source (Coney), never a game script;
-- intro.s.luac is what a stock Lua 4.0.1 `luac -s` makes of it.
Wake(3)
Scratch = 2
if Hidden then
    BindShow(0)
end

-- A field of the script's own table that shares a helper's name: defining it is not a call of the helper.
Panel = {}
function Panel.Wake()
    BindShow(1)
end
