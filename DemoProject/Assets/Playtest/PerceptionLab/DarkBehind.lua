-- Tool check (docs/Playtest.md): the thief crouches in the dark right behind the sentry, between him and the north
-- wall. No seed may end "detected"; anything else is a bug in the playtester.
return {
    name = "Dark, behind",
    time_limit = 30,
    survive = true, -- standing it out to the time limit unseen counts as a success
    jitter = 0,
    start = { at = vec3(0.3, 0.05, -9.3), yaw = 180 },
    steps = {
        { wait = 60, gait = "crouch", label = "crouch behind the sentry" },
    },
}
