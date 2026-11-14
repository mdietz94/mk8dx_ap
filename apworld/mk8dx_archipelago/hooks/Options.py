# Object classes from AP that represent different types of options that you can create
from Options import FreeText, NumericOption, Toggle, DefaultOnToggle, Choice, Range, NamedRange, OptionSet

# These helper methods allow you to determine if an option has been set, or what its value is, for any player in the multiworld
from ..Helpers import is_option_enabled, get_option_value


class CourseSelection(Choice):
    """Which courses are in the seed.

    base_game: the 48 base courses (12 cups). You start with Mushroom Cup.
    booster_course_pass: only the 48 Booster Course Pass courses (12 cups)
        and the BCP characters. You start with Golden Dash Cup.
    both: all 96 courses (24 cups) and the BCP characters.

    The two BCP choices require owning the DLC. Weight the choices to roll
    one at random, e.g. base_game: 50, booster_course_pass: 50."""
    display_name = "Course selection"
    option_base_game = 0
    option_booster_course_pass = 1
    option_both = 2
    default = 2


class IncludeTop3Locations(DefaultOnToggle):
    """Add a 'Top 3' check to every course (finish 3rd or better)."""
    display_name = "Top 3 locations"


class IncludeWinLocations(DefaultOnToggle):
    """Add a 'Win' check to every course (finish 1st). Gated in logic on
    Progressive Speed (see win_requires_speed)."""
    display_name = "Win locations"


class IncludeStarLocations(DefaultOnToggle):
    """Add '1 Star', '2 Stars' and '3 Stars' checks to every cup's Grand Prix,
    awarded like the in-game rank from the cup's point total (54+ / 57+ /
    60, i.e. 1st in all four races for 3 Stars). A better rank also checks
    the lower ones. Gated in logic like the cup win (win_requires_speed)."""
    display_name = "Grand Prix star locations"


class ShuffleCharacters(DefaultOnToggle):
    """Lock every character except Mario until its item is received. Locked
    selections are swapped to Mario when the race starts (the select screen
    is not greyed out yet)."""
    display_name = "Shuffle characters"


class ShuffleVehicles(DefaultOnToggle):
    """Lock karts, tires and gliders except the Standard set until their
    items are received."""
    display_name = "Shuffle vehicles"


class ShuffleItems(DefaultOnToggle):
    """Lock item-box items except Mushroom, Banana and Coin until their items
    are received. A locked roll becomes a Coin."""
    display_name = "Shuffle item box"


class RestrictCpuItems(Toggle):
    """Apply the item-box restrictions to CPU racers too."""
    display_name = "Restrict CPU items"


class SpeedRestriction(Choice):
    """How Progressive Speed holds you back.

    stat_cap: every kart stat (speed, acceleration, weight, handling,
    traction, mini-turbo, invincibility) is capped at starting_stat_cap
    points, as if racing on bad parts, and the kart's base top speed is
    scaled to min_speed_percent (the game's own speed stat only spans ~6%,
    so the cap alone is barely felt). Boosts scale with the kart, as they do
    for a slow combo; nothing is clamped per frame. Each Progressive Speed
    raises both; the last one removes them.

    top_speed_cap: your speed is clamped to min_speed_percent of the kart's
    top speed, rising with each Progressive Speed (the 0.2.x behaviour)."""
    display_name = "Speed restriction"
    option_stat_cap = 0
    option_top_speed_cap = 1
    default = 0


class SpeedStages(Range):
    """Number of Progressive Speed items. Each one loosens the restriction
    picked in speed_restriction; with all of them the kart is unrestricted.
    0 disables the restriction."""
    display_name = "Speed stages"
    range_start = 0
    range_end = 5
    default = 3


class StartingStatCap(Range):
    """stat_cap mode: the highest level (in points, 1-20; the in-game bars
    show 0.75 + points / 4) any stat of your kart can have with zero
    Progressive Speed items. Stats of real combos sit around 3-15 points;
    8 (2.75 bars) is a bit under the middle. The cap rises evenly to 20
    (no cap) with the last Progressive Speed."""
    display_name = "Starting stat cap"
    range_start = 1
    range_end = 19
    default = 8


class MinSpeedPercent(Range):
    """Top speed (percent of normal) with zero Progressive Speed items.
    stat_cap mode scales the kart's base top speed (boosts scale with it);
    top_speed_cap mode clamps the speed every frame."""
    display_name = "Minimum speed percent"
    range_start = 30
    range_end = 90
    default = 75


class WinRequiresSpeed(Range):
    """Progressive Speed items required in logic before 'Win' checks and
    Grand Prix cup wins are expected. Clamped to speed_stages."""
    display_name = "Win requires speed"
    range_start = 0
    range_end = 5
    default = 2


class GoalCups(Range):
    """Number of cups to win in Grand Prix (1st place overall after all
    four races) to reach the goal.
    Clamped to the number of cups in the seed."""
    display_name = "Goal: cups completed"
    range_start = 1
    range_end = 24
    default = 8


class StartingCups(Range):
    """Cups unlocked from the start (Mushroom Cup plus this many minus one
    at random)."""
    display_name = "Starting cups"
    range_start = 1
    range_end = 4
    default = 1


# This is called before any manual options are defined, in case you want to define your own with a clean slate or let Manual define over them
def before_options_defined(options: dict) -> dict:
    options["course_selection"] = CourseSelection
    options["include_top3_locations"] = IncludeTop3Locations
    options["include_win_locations"] = IncludeWinLocations
    options["include_star_locations"] = IncludeStarLocations
    options["shuffle_characters"] = ShuffleCharacters
    options["shuffle_vehicles"] = ShuffleVehicles
    options["shuffle_items"] = ShuffleItems
    options["restrict_cpu_items"] = RestrictCpuItems
    options["speed_restriction"] = SpeedRestriction
    options["speed_stages"] = SpeedStages
    options["starting_stat_cap"] = StartingStatCap
    options["min_speed_percent"] = MinSpeedPercent
    options["win_requires_speed"] = WinRequiresSpeed
    options["goal_cups"] = GoalCups
    options["starting_cups"] = StartingCups
    return options


# This is called after any manual options are defined, in case you want to see what options are defined or want to modify the defined options
def after_options_defined(options: dict) -> dict:
    return options
