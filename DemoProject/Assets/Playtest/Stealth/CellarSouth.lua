-- The Ashcombe Manor (Scenes/Stealth.pscene), Normal: the coins, then the cellar (the lock picked) for the wine and
-- the strongbox (300), then south through the dark west half and along the south wall to the gate, waiting in the
-- dark until the lantern guard (GuardB) has walked off north. Format: docs/Playtest.md.
return {
    name = "Cellar, south way",
    steps = {
        { use = "Coins", gait = "crouch", label = "take the coins" },
        { use = "Wine", label = "take the wine (cellar)" },
        { use = "Strongbox", label = "take the strongbox" },
        { to = vec3(-7, 0, 0), label = "out of the cellar, south" },
        { to = vec3(-6, 0, 11), label = "down the dark west half" },
        { to = vec3(9, 0, 12), label = "east along the south lawn" },
        { to = vec3(10, 0, 16.5), label = "into the dark by the south wall" },
        { wait_for = "GuardB", away = 10, label = "wait for the lantern guard to walk off" },
        { goal = true, label = "to the gate" },
    },
}
