-- The demo mission on Scenes/Stealth.pscene (see Scripts/Mission/Mission.lua for the format).
return {
    name = "The Ashcombe Manor",
    scene = "Scenes/Stealth.pscene",
    briefing = {
        "Lord Ashcombe keeps his silver in the great hall and his cellar key on his captain's belt.",
        "Get in over the north-west wall, take what you can and leave through the south-east gate.",
    },
    objectives = {
        { loot = { Normal = 300, Hard = 500, Expert = 700 } },
        { item = "CellarKey", text = "Pick the cellar key from the captain's belt", difficulty = { "Hard", "Expert" } },
        { no_kill = { "Cook", "Servant" }, text = "Do not kill the servants", difficulty = { "Hard" } },
        { no_kill = "all", text = "Do not kill anyone", difficulty = { "Expert" } },
        { escape = true, text = "Escape through the gate" },
    },
}
