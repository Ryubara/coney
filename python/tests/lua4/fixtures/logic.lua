-- and, or and not, as conditions and as values. Our own source (Coney).
function conditions(a, b, c, d)
    if a and b then f() end
    if a or b then f() end
    if a and b or c then f() end
    if a or b and c then f() end
    if a or (b and c) then f() end
    if a and (b or c) and d then f() end
    if not (a and b) then f() end
    if a == nil then f() end
    if a ~= nil then f() end
    while a and b do f() end
    repeat f() until a or b
end

function values(a, b, c)
    local p = a and b
    local q = a or b
    local r = a and b or c
    local s = a < b
    local t = a < b and c
    local u = not (a or b)
    local v = (a or b) == c
    local w = a and (b or c)
    f(a or b, c and d)
    return a == b, a ~= b
end
