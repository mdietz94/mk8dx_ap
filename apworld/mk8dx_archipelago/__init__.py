import logging
import typing
from typing import Optional

import Utils
import settings
from worlds.generic.Rules import forbid_items_for_player
from worlds.LauncherComponents import (
    Component,
    components,
    Type,
    launch as launch_or_subprocess,
)

from .Data import item_table, location_table, region_table, category_table, meta_table
from .Game import game_name, filler_item_name, starting_items
from .Meta import world_description, world_webworld, enable_region_diagram
from .Locations import location_id_to_name, location_name_to_id, location_name_to_location, location_name_groups, victory_names
from .Items import item_id_to_name, item_name_to_id, item_name_to_item, item_name_groups
from .DataValidation import runGenerationDataValidation, runPreFillDataValidation

from .Regions import create_regions
from .Items import MK8DXItem
from .Rules import set_rules
from .Options import mk8dx_options_data
from .Helpers import is_option_enabled, is_item_enabled, get_option_value

from BaseClasses import ItemClassification, Tutorial, Item
from Options import PerGameCommonOptions
from worlds.AutoWorld import World, WebWorld

from .hooks.World import \
    before_create_regions, after_create_regions, \
    before_create_items_starting, before_create_items_filler, after_create_items, \
    before_create_item, after_create_item, \
    before_set_rules, after_set_rules, \
    before_generate_basic, after_generate_basic, \
    before_fill_slot_data, after_fill_slot_data, before_write_spoiler
from .hooks.Data import hook_interpret_slot_data


class MK8DXSettings(settings.Group):
    """MK8DX Client settings. Lives in `~/.archipelago/host.yaml` under the
    `mk8dx_options:` key (derived by Archipelago from the apworld zip stem
    `mk8dx.apworld` -> `worlds.mk8dx`).

      mk8dx_options:
        switch_listen_host: "0.0.0.0"
        switch_listen_port: 17778
    """

    class SwitchListenHost(str):
        """Bind address for the Switch TCP server (default 0.0.0.0)."""

    class SwitchListenPort(int):
        """Port for the Switch TCP server (default 17778). The Switch mod
        discovers it over UDP; only change it if the port is taken."""

    switch_listen_host: SwitchListenHost = SwitchListenHost("0.0.0.0")
    switch_listen_port: SwitchListenPort = SwitchListenPort(17778)


class MK8DXWorld(World):
    __doc__ = world_description
    game: str = game_name
    web = world_webworld

    options_dataclass = mk8dx_options_data
    settings: typing.ClassVar[MK8DXSettings]
    data_version = 1
    required_client_version = (0, 5, 0)

    item_table = item_table
    location_table = location_table
    category_table = category_table

    item_id_to_name = item_id_to_name
    item_name_to_id = item_name_to_id
    item_name_to_item = item_name_to_item
    item_name_groups = item_name_groups

    item_counts = {}
    start_inventory = {}

    location_id_to_name = location_id_to_name
    location_name_to_id = location_name_to_id
    location_name_to_location = location_name_to_location
    location_name_groups = location_name_groups
    victory_names = victory_names

    def interpret_slot_data(self, slot_data: dict[str, any]):
        regen = False
        for key, value in slot_data.items():
            if key in self.options_dataclass.type_hints:
                getattr(self.options, key).value = value
                regen = True
        regen = hook_interpret_slot_data(self, self.player, slot_data) or regen
        return regen

    @classmethod
    def stage_assert_generate(cls, multiworld) -> None:
        runGenerationDataValidation()

    def create_regions(self):
        before_create_regions(self, self.multiworld, self.player)

        create_regions(self, self.multiworld, self.player)

        location_game_complete = self.multiworld.get_location(victory_names[0], self.player)
        location_game_complete.address = None
        location_game_complete.place_locked_item(
            MK8DXItem("__Victory__", ItemClassification.progression, None, player=self.player))

        after_create_regions(self, self.multiworld, self.player)

    def create_items(self):
        pool = []
        traps = []
        configured_item_names = self.item_id_to_name.copy()

        for name in configured_item_names.values():
            if name == "__Victory__": continue
            if name == filler_item_name: continue

            item = self.item_name_to_item[name]
            item_count = int(item.get("count", 1))

            if item.get("trap"):
                traps.append(name)

            if "category" in item:
                if not is_item_enabled(self.multiworld, self.player, item):
                    item_count = 0

            if item_count == 0: continue

            for i in range(item_count):
                new_item = self.create_item(name)
                pool.append(new_item)

            if item.get("early"):
                self.multiworld.early_items[self.player][name] = item_count
            if item.get("local"):
                if name not in self.multiworld.local_items[self.player].value:
                    self.options.local_items.value.add(name)

        pool = before_create_items_starting(pool, self, self.multiworld, self.player)

        items_started = []

        if starting_items:
            for starting_item_block in starting_items:
                if "if_previous_item" in starting_item_block:
                    matching_items = [item for item in items_started if item.name in starting_item_block["if_previous_item"]]
                    if len(matching_items) == 0:
                        continue

                items = pool

                if "items" in starting_item_block:
                    items = [item for item in pool if item.name in starting_item_block["items"]]

                if "item_categories" in starting_item_block:
                    items_in_categories = [item["name"] for item in self.item_name_to_item.values() if "category" in item and len(set(starting_item_block["item_categories"]).intersection(item["category"])) > 0]
                    items = [item for item in pool if item.name in items_in_categories]

                self.random.shuffle(items)

                if "random" in starting_item_block:
                    items = items[0:starting_item_block["random"]]

                for starting_item in items:
                    items_started.append(starting_item)
                    self.multiworld.push_precollected(starting_item)
                    pool.remove(starting_item)

        self.start_inventory = {i.name: items_started.count(i) for i in items_started}

        pool = before_create_items_filler(pool, self, self.multiworld, self.player)
        pool = self.adjust_filler_items(pool, traps)
        pool = after_create_items(pool, self, self.multiworld, self.player)

        self.multiworld.itempool += pool

    def create_item(self, name: str) -> Item:
        name = before_create_item(name, self, self.multiworld, self.player)

        item = self.item_name_to_item[name]
        classification = ItemClassification.filler

        if "trap" in item and item["trap"]:
            classification = ItemClassification.trap

        if "useful" in item and item["useful"]:
            classification = ItemClassification.useful

        if "progression" in item and item["progression"]:
            classification = ItemClassification.progression

        if "progression_skip_balancing" in item and item["progression_skip_balancing"]:
            classification = ItemClassification.progression_skip_balancing

        item_object = MK8DXItem(name, classification,
                        self.item_name_to_id[name], player=self.player)

        item_object = after_create_item(item_object, self, self.multiworld, self.player)

        return item_object

    def set_rules(self):
        before_set_rules(self, self.multiworld, self.player)

        set_rules(self, self.multiworld, self.player)

        after_set_rules(self, self.multiworld, self.player)

    def generate_basic(self):
        before_generate_basic(self, self.multiworld, self.player)

        # Handle item forbidding
        forbid_locations_map = {location['name']: location for location in location_name_to_location.values() if "dont_place_item" in location or "dont_place_item_category" in location}
        locations_with_forbid = [l for l in self.multiworld.get_unfilled_locations(player=self.player) if l.name in forbid_locations_map.keys()]
        for location in locations_with_forbid:
            location_data = forbid_locations_map[location.name]
            forbidden_item_names = []

            if "dont_place_item" in location_data:
                if len(location_data["dont_place_item"]) == 0:
                    continue
                forbidden_item_names.extend([i["name"] for i in item_name_to_item.values() if i["name"] in location_data["dont_place_item"]])

            if "dont_place_item_category" in location_data:
                if len(location_data["dont_place_item_category"]) == 0:
                    continue
                forbidden_item_names.extend([i["name"] for i in item_name_to_item.values() if "category" in i and set(i["category"]).intersection(location_data["dont_place_item_category"])])

            if len(forbidden_item_names) > 0:
                forbid_items_for_player(location, forbidden_item_names, self.player)
                forbidden_item_names.clear()

        # Handle specific item placements using fill_restrictive
        placement_locations_map = {location['name']: location for location in location_name_to_location.values() if "place_item" in location or "place_item_category" in location}
        locations_with_placements = [l for l in self.multiworld.get_unfilled_locations(player=self.player) if l.name in placement_locations_map.keys()]
        for location in locations_with_placements:
            location_data = placement_locations_map[location.name]
            eligible_items = []

            if "place_item" in location_data:
                if len(location_data["place_item"]) == 0:
                    continue
                eligible_items = [item for item in self.multiworld.itempool if item.name in location_data["place_item"] and item.player == self.player]
                if len(eligible_items) == 0:
                    raise Exception("Could not find a suitable item to place at %s. No items that match %s." % (location_data["name"], ", ".join(location_data["place_item"])))

            if "place_item_category" in location_data:
                if len(location_data["place_item_category"]) == 0:
                    continue
                eligible_item_names = [i["name"] for i in item_name_to_item.values() if "category" in i and set(i["category"]).intersection(location_data["place_item_category"])]
                eligible_items = [item for item in self.multiworld.itempool if item.name in eligible_item_names and item.player == self.player]
                if len(eligible_items) == 0:
                    raise Exception("Could not find a suitable item to place at %s. No items that match categories %s." % (location_data["name"], ", ".join(location_data["place_item_category"])))

            if "dont_place_item" in location_data:
                if len(location_data["dont_place_item"]) == 0:
                    continue
                eligible_items = [item for item in eligible_items if item.name not in location_data["dont_place_item"]]
                if len(eligible_items) == 0:
                    raise Exception("Could not find a suitable item to place at %s. No items that match placed_items(_category) because of forbidden %s." % (location_data["name"], ", ".join(location_data["dont_place_item"])))

            if "dont_place_item_category" in location_data:
                if len(location_data["dont_place_item_category"]) == 0:
                    continue
                forbidden_item_names = [i["name"] for i in item_name_to_item.values() if "category" in i and set(i["category"]).intersection(location_data["dont_place_item_category"])]
                eligible_items = [item for item in eligible_items if item.name not in forbidden_item_names]
                if len(eligible_items) == 0:
                    raise Exception("Could not find a suitable item to place at %s. No items that match placed_items(_category) because of forbidden categories %s." % (location_data["name"], ", ".join(location_data["dont_place_item_category"])))
                forbidden_item_names.clear()

            if len(eligible_items) == 0:
                raise Exception("Custom item placement at location %s failed." % (location_data["name"]))

            item_to_place = self.random.choice(eligible_items)
            location.place_locked_item(item_to_place)
            self.multiworld.itempool.remove(item_to_place)

        after_generate_basic(self, self.multiworld, self.player)

        if enable_region_diagram:
            from Utils import visualize_regions
            visualize_regions(self.multiworld.get_region("Menu", self.player), f"{self.game}_{self.player}.puml")

    def pre_fill(self):
        runPreFillDataValidation(self, self.multiworld)

    def fill_slot_data(self):
        slot_data = before_fill_slot_data({}, self, self.multiworld, self.player)

        common_options = set(PerGameCommonOptions.type_hints.keys())
        for option_key, _ in self.options_dataclass.type_hints.items():
            if option_key in common_options:
                continue
            slot_data[option_key] = get_option_value(self.multiworld, self.player, option_key)

        slot_data = after_fill_slot_data(slot_data, self, self.multiworld, self.player)

        return slot_data

    def write_spoiler(self, spoiler_handle):
        before_write_spoiler(self, self.multiworld, spoiler_handle)

    ###
    # Non-standard AP world methods
    ###

    def adjust_filler_items(self, item_pool, traps):
        extras = len(self.multiworld.get_unfilled_locations(player=self.player)) - len(item_pool)

        if extras > 0:
            trap_percent = get_option_value(self.multiworld, self.player, "filler_traps")
            if not traps:
                trap_percent = 0

            trap_count = extras * trap_percent // 100
            filler_count = extras - trap_count

            for _ in range(0, trap_count):
                extra_item = self.create_item(self.random.choice(traps))
                item_pool.append(extra_item)

            for _ in range(0, filler_count):
                extra_item = self.create_item(filler_item_name)
                item_pool.append(extra_item)
        elif extras < 0:
            logging.warning(f"{self.game} has more items than locations. {abs(extras)} non-progression items will be removed at random.")
            fillers = [item for item in item_pool if item.classification == ItemClassification.filler]
            traps = [item for item in item_pool if item.classification == ItemClassification.trap]
            useful = [item for item in item_pool if item.classification == ItemClassification.useful]
            self.random.shuffle(fillers)
            self.random.shuffle(traps)
            self.random.shuffle(useful)
            for _ in range(0, abs(extras)):
                popped = None
                if fillers:
                    popped = fillers.pop()
                elif traps:
                    popped = traps.pop()
                elif useful:
                    popped = useful.pop()
                else:
                    logging.warning("Could not remove enough non-progression items from the pool.")
                    break
                item_pool.remove(popped)

        return item_pool

    def get_item_counts(self, player: Optional[int] = None, reset: bool = False) -> dict[str, int]:
        """returns the player real item count"""
        if player is None:
            player = self.player
        if not self.item_counts.get(player, {}) or reset:
            real_pool = self.multiworld.get_items()
            self.item_counts[player] = {i.name: real_pool.count(i) for i in real_pool if i.player == player}
        return self.item_counts.get(player)


###
# Launcher client registration
###

CLIENT_DISPLAY_NAME = "MK8DX Client"


def launch_mk8dx_client(*args):
    """Archipelago Launcher entry point. Lazy-imports the client so headless
    generation hosts never pay for Kivy."""
    from .client.main import launch as client_launch
    launch_or_subprocess(client_launch, name="MK8DXClient", args=args)


def run_setup_wizard() -> None:
    """Subprocess entry for the setup wizard (`/setup`, and the client's
    first run). Module level so multiprocessing can pickle it by name."""
    from ._setup.wizard import run_wizard
    run_wizard()


def add_client_to_launcher() -> None:
    # No file_identifier: the mod needs nothing per seed, so there is no
    # patch file to open. (smo_archipelago generated one and archipelago.gg
    # then presented it as a "patch" players had to download.)
    for c in components:
        if c.display_name == CLIENT_DISPLAY_NAME:
            return
    components.append(Component(
        CLIENT_DISPLAY_NAME,
        func=launch_mk8dx_client,
        component_type=Type.CLIENT,
        game_name=game_name,
        description="Bridges a Switch or Ryujinx running Mario Kart 8 Deluxe to Archipelago. "
                    "Type /setup in it to install the Switch mod.",
    ))


add_client_to_launcher()
