-- Suspicion at the threshold (docs/Playtest.md): the thief stands still 4 m beside the guard's patrol, which passes
-- him at 4-9 m. Barely made out (B just above 1).
return {
    name = "Haze, beside the patrol",
    time_limit = 20,
    survive = true, -- standing it out to the time limit unseen counts as a success
    start = { at = vec3(13.0, 0.05, 0.0) },
    steps = {
        { wait = 30, gait = "walk", label = "stand beside the patrol" },
    },
}
