-- The first mission, its blockout (Scenes/Mission1.pscene, docs/research/MissionOne.md): from the tavern through
-- the old town and the church's catacombs, by a collapsed tunnel into the mayor's vineyard and manor, and back the
-- same way. The way out through the cart of the feast's leftovers comes with its own package.
return {
    name = "The Mayor's Leftovers",
    scene = "Scenes/Mission1.pscene",
    briefing = {
        "Last night the mayor feasted the city council at his manor beyond the walls. Tonight the house sleeps it off.",
        "Leave the tavern and cross the old town to the church: its catacombs reach his vineyard by a tunnel",
        "that caved in years ago. His ledger of who paid him what lies in the library upstairs. Bring it back.",
    },
    objectives = {
        { item = "Ledger", text = "Steal the mayor's ledger from his library" },
        { loot = { Normal = 400, Hard = 700, Expert = 1000 } },
        { item = "SignetRing", text = "Take the mayor's signet ring from his bedroom", difficulty = { "Hard", "Expert" } },
        { no_kill = "all", text = "Do not kill anyone", difficulty = { "Expert" } },
        { escape = true, text = "Get away the way you came: through the vineyard into the tunnel" },
    },
}
