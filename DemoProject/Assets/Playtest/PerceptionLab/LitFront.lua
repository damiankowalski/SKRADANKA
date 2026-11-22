-- Tool check (docs/Playtest.md): the thief stands in the campfire's light in front of the sentry, 9 m away. Every
-- seed must end "detected"; anything else is a bug in the playtester.
return {
    name = "Lit, in front",
    time_limit = 30,
    survive = true, -- standing it out to the time limit unseen counts as a success
    jitter = 0,
    start = { at = vec3(1.0, 0.05, 1.0), yaw = 0 },
    steps = {
        { wait = 60, gait = "walk", label = "stand by the fire" },
    },
}
