-- Mood lab scene (Scenes/MoodLab.pscene): applies the variant named by the game value "mood"
-- (Base when unset) when play starts, so a variant can be walked through in the runtime:
--   PenumbraRuntime --project DemoProject --scene Scenes/MoodLab.pscene --set mood=Mixed
-- The variants themselves are in Scripts/MoodLab/Variants.lua.

local Variants = require("Scripts/MoodLab/Variants")

local MoodLab = {}

function MoodLab:on_create()
    Variants.apply(game.get("mood", "Base"))
end

return MoodLab
