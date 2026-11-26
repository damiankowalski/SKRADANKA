-- Suspicion at the threshold (docs/Playtest.md): the place of "Haze, beyond the patrol", crouched: a smaller
-- silhouette, the stimulus lower still.
return {
    name = "Haze, beyond the patrol, crouched",
    time_limit = 20,
    survive = true, -- standing it out to the time limit unseen counts as a success
    start = { at = vec3(9.0, 0.05, 8.0) },
    steps = {
        { wait = 30, gait = "crouch", label = "crouch against the haze" },
    },
}
