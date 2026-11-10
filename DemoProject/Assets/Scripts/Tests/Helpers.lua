-- Shared helpers for scripted test scenes (loaded with require).
local Helpers = {}

-- Runs fn as a coroutine; inside it, Helpers.wait(seconds) suspends across frames.
function Helpers.sequence(fn)
    local routine = coroutine.create(fn)
    local waiting = 0
    return function(dt)
        if coroutine.status(routine) == "dead" then return false end
        waiting = waiting - dt
        if waiting > 0 then return true end
        local ok, result = coroutine.resume(routine)
        if not ok then
            test.check(false, "test sequence error: " .. tostring(result))
            test.complete()
            return false
        end
        waiting = result or 0
        return coroutine.status(routine) ~= "dead"
    end
end

function Helpers.wait(seconds)
    coroutine.yield(seconds)
end

function Helpers.count(t)
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    return n
end

return Helpers
