-- Suspicion at the threshold (docs/Playtest.md): the thief stands still in plain view 5 m beyond the end of the guard's
-- patrol, against the haze the torches light. The guard barely makes him out (B just above 1) at 5-11 m; the report's
-- reaction times show how long he takes to react to that.
return {
    name = "Haze, beyond the patrol",
    time_limit = 20,
    survive = true, -- standing it out to the time limit unseen counts as a success
    start = { at = vec3(9.0, 0.05, 8.0) },
    steps = {
        { wait = 30, gait = "walk", label = "stand against the haze" },
    },
}
