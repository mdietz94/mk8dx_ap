# Object classes from AP core, to represent an entire MultiWorld and this individual World that's part of it
from worlds.AutoWorld import World
from BaseClasses import ItemClassification, MultiWorld

from ..Items import MK8DXItem
from ..Locations import MK8DXLocation
from ..Data import game_table, item_table, location_table, region_table
from ..Helpers import is_location_enabled, get_option_value

import logging

PROGRESSIVE_SPEED = "Progressive Speed"
TROPHY = "Trophy"
CUP_CATEGORY = "Cups"
# First one present in the item pool is precollected (base game, then BCP).
STARTING_CUPS = ("Mushroom Cup", "Golden Dash Cup")


def before_create_regions(world: World, multiworld: MultiWorld, player: int):
    pass


def after_create_regions(world: World, multiworld: MultiWorld, player: int):
    # Number of cup-completion locations actually in this seed (course_selection);
    # GoalTrophies() clamps the goal to it.
    world.cup_completion_count = sum(
        1 for loc in multiworld.get_locations(player)
        if loc.name.startswith("Complete ") and loc.name.endswith("(Grand Prix)")
    )


def before_create_items_starting(item_pool: list, world: World, multiworld: MultiWorld, player: int) -> list:
    return item_pool


def _precollect(item, item_pool: list, world: World, multiworld: MultiWorld) -> None:
    multiworld.push_precollected(item)
    item_pool.remove(item)
    world.start_inventory[item.name] = world.start_inventory.get(item.name, 0) + 1


def before_create_items_filler(item_pool: list, world: World, multiworld: MultiWorld, player: int) -> list:
    # Starting cup: Mushroom Cup, or Golden Dash Cup when course_selection
    # leaves the base cups out (the first cup present in the pool).
    world.starting_cup = next((n for n in STARTING_CUPS if any(i.name == n for i in item_pool)), STARTING_CUPS[0])
    for it in item_pool:
        if it.name == world.starting_cup:
            _precollect(it, item_pool, world, multiworld)
            break

    # Extra starting cups: starting_cups - 1 random cups beyond the first.
    extra = int(get_option_value(multiworld, player, "starting_cups")) - 1
    if extra > 0:
        cups = [i for i in item_pool
                if CUP_CATEGORY in world.item_name_to_item.get(i.name, {}).get("category", [])]
        world.random.shuffle(cups)
        for it in cups[:extra]:
            _precollect(it, item_pool, world, multiworld)

    # Progressive Speed count follows the option.
    stages = int(get_option_value(multiworld, player, "speed_stages"))
    speed = [i for i in item_pool if i.name == PROGRESSIVE_SPEED]
    for it in speed[stages:]:
        item_pool.remove(it)

    # One Trophy per cup-completion location that exists in this seed (they
    # are place_item-locked there; surplus trophies would float into the
    # pool and satisfy the goal without racing).
    want = getattr(world, "cup_completion_count", 0)
    trophies = [i for i in item_pool if i.name == TROPHY]
    for it in trophies[want:]:
        item_pool.remove(it)
    return item_pool


def after_create_items(item_pool: list, world: World, multiworld: MultiWorld, player: int) -> list:
    return item_pool


def before_create_item(item_name: str, world: World, multiworld: MultiWorld, player: int) -> str:
    return item_name


def after_create_item(item: MK8DXItem, world: World, multiworld: MultiWorld, player: int) -> MK8DXItem:
    return item


def before_set_rules(world: World, multiworld: MultiWorld, player: int):
    pass


def after_set_rules(world: World, multiworld: MultiWorld, player: int):
    pass


def before_generate_basic(world: World, multiworld: MultiWorld, player: int) -> list:
    pass


def after_generate_basic(world: World, multiworld: MultiWorld, player: int):
    pass


def before_fill_slot_data(slot_data: dict, world: World, multiworld: MultiWorld, player: int) -> dict:
    return slot_data


def after_fill_slot_data(slot_data: dict, world: World, multiworld: MultiWorld, player: int) -> dict:
    slot_data["cup_completion_count"] = getattr(world, "cup_completion_count", 0)
    slot_data["starting_cup"] = getattr(world, "starting_cup", STARTING_CUPS[0])
    slot_data["goal_trophies"] = max(1, min(int(get_option_value(multiworld, player, "goal_cups")),
                                            slot_data["cup_completion_count"] or 1))
    return slot_data


def before_write_spoiler(world: World, multiworld: MultiWorld, spoiler_handle) -> None:
    pass
