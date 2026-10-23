-- Functions: nested closures, upvalues, varargs, methods, tail calls. Our own source (Coney).
function outer(x, y)
    local z = x + y
    local inner = function(w) return %x + %z + w + %global_name end
    return inner(1), function() return %inner end
end

function vararg(first, ...)
    f(first, arg)
    return arg.n
end

local object = {}
function object:method(a)
    self.value = a
    return self:other(a)
end
function object.static(a) return a end

handlers = {
    on_enter = function(who) f(who) end,
    on_leave = function(who, where) g(who, where) end,
}
