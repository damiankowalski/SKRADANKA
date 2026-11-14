-- The cloister (Scenes/Cloister.pscene): get from the east wing to the west wing, past the two
-- guards walking the ring around the garden (see Scripts/Mission/Mission.lua for the format).
return {
    name = "The Cloister",
    scene = "Scenes/Cloister.pscene",
    briefing = {
        "Two watchmen walk the covered walk around the abbot's garden, each his own way round.",
        "Slip in at the end of the east wing and reach the west wing unseen.",
    },
    objectives = {
        { escape = true, text = "Reach the end of the west wing" },
    },
}
