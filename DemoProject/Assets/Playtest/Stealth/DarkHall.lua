-- The Ashcombe Manor, Normal: put out the hall lamps at the switch by the door, take the goblet, the candlestick
-- and the jewel box from the hall (450), then south through the middle and along the south lawn to the gate,
-- waiting for the lantern guard (GuardB) to walk off. Format: docs/Playtest.md.
return {
    name = "Dark hall",
    steps = {
        { to = vec3(-4, 0, -10), gait = "crouch", label = "east across the north yard" },
        { use = "HallSwitch", lit = false, label = "put out the hall lamps" },
        { use = "GoldGoblet", label = "take the gold goblet" },
        { use = "Candlestick", label = "take the candlestick" },
        { use = "JewelBox", label = "take the jewel box" },
        { to = vec3(3, 0, -3), label = "out of the hall, south-west" },
        { to = vec3(0, 0, 3), label = "across the dark middle" },
        { to = vec3(9, 0, 12), label = "south-east over the lawn" },
        { to = vec3(10, 0, 16.5), label = "into the dark by the south wall" },
        { wait_for = "GuardB", away = 10, label = "wait for the lantern guard to walk off" },
        { goal = true, label = "to the gate" },
    },
}
