
function branches(a, b)
    if a then f() end
    if a then f() else g() end
    if a then f() elseif b then g() elseif a == b then h() else i() end
    if a then if b then f() end else g() end
    if a then end
    if not a then end
    if a < b then f() elseif a <= b then g() elseif a > b then h() elseif a >= b then i() end
    if a ~= b then f() end
end

function loops(n)
    local total = 0
    while total < n do total = total + 1 end
    while 1 do
        if total > n then break end
        total = total + 1
    end
    while 1 do
        if total then f() else g() end
    end
end

function more_loops(n, t)
    local total = 0
    repeat
        local step = 2
        total = total + step
    until total > n
    for i = 1, n do total = total + i end
    for i = n, 1, -1 do
        if i == 3 then break end
        total = total - i
    end
    for k, v in t do
        local w = v
        if k then break end
        f(k, w)
    end
    while total > 0 do
        local half = total / 2
        if half < 1 then break end
        total = half
    end
    return total
end

function blocks()
    do
        local inner = 1
        f(inner)
    end
    local after = 2
    return after
end

function returns(a)
    if a then return 1, 2 end
    if a == 2 then return f(a) end
    return a, g(a)
end
