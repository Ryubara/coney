
function branches(p1, p2)
    if p1 then
        f()
    end
    if p1 then
        f()
    else
        g()
    end
    if p1 then
        f()
    elseif p2 then
        g()
    elseif p1 == p2 then
        h()
    else
        i()
    end
    if p1 then
        if p2 then
            f()
        end
    else
        g()
    end
    if p1 then
    end
    if not p1 then
    end
    if p1 < p2 then
        f()
    elseif p1 <= p2 then
        g()
    elseif p1 > p2 then
        h()
    elseif p1 >= p2 then
        i()
    end
    if p1 ~= p2 then
        f()
    end
end
function loops(p1)
    local l1 = 0
    while l1 < p1 do
        l1 = l1 + 1
    end
    while 1 do
        if l1 > p1 then
            break
        end
        l1 = l1 + 1
    end
    while 1 do
        if l1 then
            f()
        else
            g()
        end
    end
end
function more_loops(p1, p2)
    local l1 = 0
    repeat
        local l2 = 2
        l1 = l1 + l2
    until l1 > p1
    for i2 = 1, p1 do
        l1 = l1 + i2
    end
    for i3 = p1, 1, -1 do
        if i3 == 3 then
            break
        end
        l1 = l1 - i3
    end
    for k, v in p2 do
        local l3 = v
        if k then
            break
        end
        f(k, l3)
    end
    while l1 > 0 do
        local l4 = l1 / 2
        if l4 < 1 then
            break
        end
        l1 = l4
    end
    return l1
end
function blocks()
    do
        local l1 = 1
        f(l1)
    end
    local l2 = 2
    return l2
end
function returns(p1)
    if p1 then
        return 1, 2
    end
    if p1 == 2 then
        return f(p1)
    end
    return p1, g(p1)
end
