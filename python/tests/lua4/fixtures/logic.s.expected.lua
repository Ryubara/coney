
function conditions(p1, p2, p3, p4)
    if p1 ~= nil and p2 then
        f()
    end
    if p1 or p2 then
        f()
    end
    if p1 and p2 or p3 then
        f()
    end
    if p1 or p2 and p3 then
        f()
    end
    if p1 or (p2 and p3) then
        f()
    end
    if p1 and (p2 or p3) and p4 then
        f()
    end
    if not p1 or not p2 then
        f()
    end
    if not p1 then
        f()
    end
    if p1 then
        f()
    end
    while p1 and p2 do
        f()
    end
    repeat
        f()
    until p1 or p2
end
function values(p1, p2, p3)
    local l1 = p1 and p2
    local l2 = p1 or p2
    local l3 = p1 and p2 or p3
    local l4 = p1 < p2
    local l5 = p1 < p2 and p3
    local l6 = not p1 and not p2
    local l7 = (p1 or p2) == p3
    local l8 = p1 and (p2 or p3)
    f(p1 or p2, p3 and d)
    return p1 == p2, p1 ~= p2
end
