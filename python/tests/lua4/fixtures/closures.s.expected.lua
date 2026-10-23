
function outer(p1, p2)
    local l1 = p1 + p2
    local l2 = function(p3)
        return %p1 + %l1 + p3 + %global_name
    end
    return l2(1), function()
        return %l2
    end
end
function vararg(p1, ...)
    f(p1, arg)
    return arg.n
end
local l1 = {}
function l1.method(p1, p2)
    p1.value = p2
    return p1:other(p2)
end
function l1.static(p1)
    return p1
end
handlers = {
    on_enter = function(p1)
        f(p1)
    end,
    on_leave = function(p1, p2)
        g(p1, p2)
    end
}
