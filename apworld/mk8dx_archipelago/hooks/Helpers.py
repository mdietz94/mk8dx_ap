from typing import Optional
from BaseClasses import MultiWorld
from ..Locations import MK8DXLocation
from ..Items import MK8DXItem


# Return True to enable the category, False to disable it, or None to use the default behavior
def before_is_category_enabled(multiworld: MultiWorld, player: int, category_name: str) -> Optional[bool]:
    return None


# Return True to enable the item, False to disable it, or None to use the default behavior
def before_is_item_enabled(multiworld: MultiWorld, player: int, item: MK8DXItem) -> Optional[bool]:
    return None


# Return True to enable the location, False to disable it, or None to use the default behavior
def before_is_location_enabled(multiworld: MultiWorld, player: int, location: MK8DXLocation) -> Optional[bool]:
    return None
