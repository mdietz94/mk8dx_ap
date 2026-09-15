
from BaseClasses import Tutorial
from worlds.AutoWorld import World, WebWorld
from .Data import meta_table

##############
# Meta Classes
##############
class MK8DXWeb(WebWorld):
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "A guide to setting up Mario Kart 8 Deluxe for Archipelago multiworld games.",
        "English",
        "setup_en.md",
        "setup/en",
        ["Max Dietz"]
    )]

######################################
# Convert meta.json data to properties
######################################
def set_world_description(base_doc: str) -> str:
    if meta_table.get("docs", {}).get("apworld_description", None) is None:
        return base_doc

    if isinstance(meta_table["docs"]["apworld_description"], str):
        base_doc = meta_table["docs"]["apworld_description"]
    else:
        fullstring = ""
        for line in meta_table["docs"]["apworld_description"]:
            fullstring += "\n" + line
        base_doc = fullstring
    return base_doc

def set_world_webworld(web: WebWorld) -> WebWorld:
    if meta_table.get("docs", {}).get("web", {}):
        Web_Config = meta_table["docs"]["web"]

        web.theme = Web_Config.get("theme", web.theme)
        web.game_info_languages = Web_Config.get("game_info_languages", web.game_info_languages)
        web.options_presets = Web_Config.get("options_presets", web.options_presets)
        web.options_page = Web_Config.get("options_page", web.options_page)
        if hasattr(web, 'bug_report_page'):
            web.bug_report_page = Web_Config.get("bug_report_page", web.bug_report_page)
        else:
            web.bug_report_page = Web_Config.get("bug_report_page", None)

        if Web_Config.get("tutorials", []):
            tutorials = []
            for tutorial in Web_Config.get("tutorials", []):
                # Converting json to Tutorials
                tutorials.append(Tutorial(
                    tutorial.get("name", "Multiworld Setup Guide"),
                    tutorial.get("description", "A guide to setting up Mario Kart 8 Deluxe for Archipelago multiworld games."),
                    tutorial.get("language", "English"),
                    tutorial.get("file_name", "setup_en.md"),
                    tutorial.get("link", "setup/en"),
                    tutorial.get("authors", [meta_table.get("creator", meta_table.get("player", "Unknown"))])
                ))
            web.tutorials = tutorials
    return web

#################
# Meta Properties
#################
world_description: str = set_world_description("""
    Mario Kart 8 Deluxe for Archipelago on a modded Nintendo Switch (or Ryujinx).
    An in-game module reports race results automatically and enforces cup, character,
    vehicle, item and top-speed restrictions until the matching AP items are received.
    """)
world_webworld: MK8DXWeb = set_world_webworld(MK8DXWeb())

enable_region_diagram = bool(meta_table.get("enable_region_diagram", False))
