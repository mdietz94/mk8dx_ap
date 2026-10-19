from typing import Optional
from BaseClasses import MultiWorld
from ..Locations import MK8DXLocation
from ..Items import MK8DXItem

# hooks/Options.py CourseSelection values. Not imported: hooks/Options.py
# imports ..Helpers, which imports this module (circular).
BASE_GAME_ONLY, BCP_ONLY, BOTH = 0, 1, 2
BASE_GAME_CATEGORY = "Base Game"
BCP_CATEGORY = "Booster Course Pass"


def course_selection(multiworld: MultiWorld, player: int) -> int:
    option = getattr(multiworld.worlds[player].options, "course_selection", None)
    return BOTH if option is None else int(option.value)


# Return True to enable the category, False to disable it, or None to use the default behavior
def before_is_category_enabled(multiworld: MultiWorld, player: int, category_name: str) -> Optional[bool]:
    # course_selection is a three-way Choice, which a categories.json
    # yaml_option (truthiness only) cannot express.
    if category_name == BASE_GAME_CATEGORY:
        return course_selection(multiworld, player) != BCP_ONLY
    if category_name == BCP_CATEGORY:
        return course_selection(multiworld, player) != BASE_GAME_ONLY
    return None


# Return True to enable the item, False to disable it, or None to use the default behavior
def before_is_item_enabled(multiworld: MultiWorld, player: int, item: MK8DXItem) -> Optional[bool]:
    return None


# Return True to enable the location, False to disable it, or None to use the default behavior
def before_is_location_enabled(multiworld: MultiWorld, player: int, location: MK8DXLocation) -> Optional[bool]:
    return None
