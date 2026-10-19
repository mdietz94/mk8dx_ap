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


class SpeedStages(Range):
    """Number of Progressive Speed items. Your top speed starts at
    min_speed_percent and reaches 100% when all stages are collected.
    0 disables the speed cap."""
    display_name = "Speed stages"
    range_start = 0
    range_end = 5
    default = 3


class MinSpeedPercent(Range):
    """Top speed (percent of normal) with zero Progressive Speed items."""
    display_name = "Minimum speed percent"
    range_start = 30
    range_end = 90
    default = 60


class WinRequiresSpeed(Range):
    """Progressive Speed items required in logic before 'Win' checks are
    expected. Clamped to speed_stages."""
    display_name = "Win requires speed"
    range_start = 0
    range_end = 5
    default = 2


class GoalCups(Range):
    """Number of cups to complete in Grand Prix (all four races) to win.
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
    options["shuffle_characters"] = ShuffleCharacters
    options["shuffle_vehicles"] = ShuffleVehicles
    options["shuffle_items"] = ShuffleItems
    options["restrict_cpu_items"] = RestrictCpuItems
    options["speed_stages"] = SpeedStages
    options["min_speed_percent"] = MinSpeedPercent
    options["win_requires_speed"] = WinRequiresSpeed
    options["goal_cups"] = GoalCups
    options["starting_cups"] = StartingCups
    return options


# This is called after any manual options are defined, in case you want to see what options are defined or want to modify the defined options
def after_options_defined(options: dict) -> dict:
    return options
