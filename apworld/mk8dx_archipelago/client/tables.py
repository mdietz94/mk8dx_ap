"""Static MK8DX tables shared by the apworld generator and the client.

Single source of truth for cup / course / driver / vehicle / item names.
`scripts/gen_apworld_data.py` renders data/{items,locations,regions,
categories}.json from these; the client uses them to map wire ids and
learned names to AP item / location names.

Two kinds of identifier appear here:

* AP names (English course / character / part names) - what shows in the
  Archipelago UI. These match the names the existing MK8DX Manual world and
  the Mario Kart Wii apworld use.
* Internal names (`Gu_FirstCircuit`, `Kinopio`, `DrvChr01`...) - the
  functional identifiers the game's own resources and PlayReport telemetry
  use. The Switch mod reports these, so the mapping to AP names lives here.

Numeric enum ids (mush::ECourseID / EDriverID / ...) are deliberately NOT
hard-coded: the public headers leave those enums empty. The client learns
`id -> internal name` at runtime from the mod (`id_table` / `course_name`
messages) and persists them; see client/learned.py.
"""

from __future__ import annotations

from dataclasses import dataclass

GAME_NAME = "Mario Kart 8 Deluxe"

# ---------------------------------------------------------------------------
# Cups (ui::ECup order; AP cup index = ECup - 1)
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Cup:
    index: int          # 0..23 == ECup - 1
    name: str           # AP name ("Mushroom Cup")
    bcp: bool           # Booster Course Pass


CUPS: list[Cup] = [
    Cup(0, "Mushroom Cup", False), Cup(1, "Flower Cup", False),
    Cup(2, "Star Cup", False), Cup(3, "Special Cup", False),
    Cup(4, "Shell Cup", False), Cup(5, "Banana Cup", False),
    Cup(6, "Leaf Cup", False), Cup(7, "Lightning Cup", False),
    Cup(8, "Egg Cup", False), Cup(9, "Triforce Cup", False),
    Cup(10, "Crossing Cup", False), Cup(11, "Bell Cup", False),
    Cup(12, "Golden Dash Cup", True), Cup(13, "Lucky Cat Cup", True),
    Cup(14, "Turnip Cup", True), Cup(15, "Propeller Cup", True),
    Cup(16, "Rock Cup", True), Cup(17, "Moon Cup", True),
    Cup(18, "Fruit Cup", True), Cup(19, "Boomerang Cup", True),
    Cup(20, "Feather Cup", True), Cup(21, "Cherry Cup", True),
    Cup(22, "Acorn Cup", True), Cup(23, "Spiny Cup", True),
]

# ---------------------------------------------------------------------------
# Courses. `internal` is the romfs / PlayReport identifier stem; the mod
# reports CourseInfo::mCourseIconResource (e.g. "tc_Course_Gu_FirstCircuit")
# and the PlayReport "Course" field, both of which contain this stem. Empty
# string = not yet known; the client then falls back to learned tables.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Course:
    cup: int            # cup index
    slot: int           # 0..3 within the cup
    name: str           # AP name
    internal: str       # internal stem ("Gu_FirstCircuit"); "" if unknown


def _cup(cup: int, entries: list[tuple[str, str]]) -> list[Course]:
    return [Course(cup, i, name, internal) for i, (name, internal) in enumerate(entries)]


COURSES: list[Course] = (
    _cup(0, [("Mario Kart Stadium", "Gu_FirstCircuit"), ("Water Park", "Gu_WaterPark"),
             ("Sweet Sweet Canyon", "Gu_Cake"), ("Thwomp Ruins", "Gu_DossunIseki")]) +
    _cup(1, [("Mario Circuit", "Gu_MarioCircuit"), ("Toad Harbor", "Gu_City"),
             ("Twisted Mansion", "Gu_HorrorHouse"), ("Shy Guy Falls", "Gu_Expert")]) +
    _cup(2, [("Sunshine Airport", "Gu_Airport"), ("Dolphin Shoals", "Gu_Ocean"),
             ("Electrodrome", "Gu_Techno"), ("Mount Wario", "Gu_SnowMountain")]) +
    _cup(3, [("Cloudtop Cruise", "Gu_Cloud"), ("Bone-Dry Dunes", "Gu_Desert"),
             ("Bowser's Castle", "Gu_BowserCastle"), ("Rainbow Road", "Gu_RainbowRoad")]) +
    _cup(4, [("Wii Moo Moo Meadows", "Gwii_MooMooMeadows"), ("GBA Mario Circuit", "Gagb_MarioCircuit"),
             ("DS Cheep Cheep Beach", "Gds_PukupukuBeach"), ("N64 Toad's Turnpike", "G64_KinopioHighway")]) +
    _cup(5, [("GCN Dry Dry Desert", "Ggc_DryDryDesert"), ("SNES Donut Plains 3", "Gsfc_DonutsPlain3"),
             ("N64 Royal Raceway", "G64_PeachCircuit"), ("3DS DK Jungle", "G3ds_DKJungle")]) +
    _cup(6, [("DS Wario Stadium", "Gds_WarioStadium"), ("GCN Sherbet Land", "Ggc_SherbetLand"),
             ("3DS Music Park", "G3ds_MusicPark"), ("N64 Yoshi Valley", "G64_YoshiValley")]) +
    _cup(7, [("DS Tick-Tock Clock", "Gds_TickTockClock"), ("3DS Piranha Plant Slide", "G3ds_PackunSlider"),
             ("Wii Grumble Volcano", "Gwii_GrumbleVolcano"), ("N64 Rainbow Road", "G64_RainbowRoad")]) +
    _cup(8, [("GCN Yoshi Circuit", "Dgc_YoshiCircuit"), ("Excitebike Arena", "Du_ExciteBike"),
             ("Dragon Driftway", "Du_DragonRoad"), ("Mute City", "Du_MuteCity")]) +
    _cup(9, [("Wii Wario's Gold Mine", "Dwii_WariosMine"), ("SNES Rainbow Road", "Dsfc_RainbowRoad"),
             ("Ice Ice Outpost", "Du_IcePark"), ("Hyrule Circuit", "Du_Hyrule")]) +
    _cup(10, [("GCN Baby Park", "Dgc_BabyPark"), ("GBA Cheese Land", "Dagb_CheeseLand"),
              ("Wild Woods", "Du_Woods"), ("Animal Crossing", "Du_Animal")]) +
    _cup(11, [("3DS Neo Bowser City", "D3ds_NeoBowserCity"), ("GBA Ribbon Road", "Dagb_RibbonRoad"),
              ("Super Bell Subway", "Du_Metro"), ("Big Blue", "Du_BigBlue")]) +
    # Booster Course Pass. Internal stems are "Cnsw_<wave><slot>": wave 1..6,
    # slot 1..8 in cup-menu order (slots 1-4 = the wave's first cup). Names per
    # NextendoNetwork/Ryujinx-Nextendo NextendoMk8Courses.cs (PlayReport table).
    _cup(12, [("Tour Paris Promenade", "Cnsw_11"), ("3DS Toad Circuit", "Cnsw_12"),
              ("N64 Choco Mountain", "Cnsw_13"), ("Wii Coconut Mall", "Cnsw_14")]) +
    _cup(13, [("Tour Tokyo Blur", "Cnsw_15"), ("DS Shroom Ridge", "Cnsw_16"),
              ("GBA Sky Garden", "Cnsw_17"), ("Ninja Hideaway", "Cnsw_18")]) +
    _cup(14, [("Tour New York Minute", "Cnsw_21"), ("SNES Mario Circuit 3", "Cnsw_22"),
              ("N64 Kalimari Desert", "Cnsw_23"), ("DS Waluigi Pinball", "Cnsw_24")]) +
    _cup(15, [("Tour Sydney Sprint", "Cnsw_25"), ("GBA Snow Land", "Cnsw_26"),
              ("Wii Mushroom Gorge", "Cnsw_27"), ("Sky-High Sundae", "Cnsw_28")]) +
    _cup(16, [("Tour London Loop", "Cnsw_31"), ("GBA Boo Lake", "Cnsw_32"),
              ("3DS Rock Rock Mountain", "Cnsw_33"), ("Wii Maple Treeway", "Cnsw_34")]) +
    _cup(17, [("Tour Berlin Byways", "Cnsw_35"), ("DS Peach Gardens", "Cnsw_36"),
              ("Merry Mountain", "Cnsw_37"), ("3DS Rainbow Road", "Cnsw_38")]) +
    _cup(18, [("Tour Amsterdam Drift", "Cnsw_41"), ("GBA Riverside Park", "Cnsw_42"),
              ("Wii DK Summit", "Cnsw_43"), ("Yoshi's Island", "Cnsw_44")]) +
    _cup(19, [("Tour Bangkok Rush", "Cnsw_45"), ("DS Mario Circuit", "Cnsw_46"),
              ("GCN Waluigi Stadium", "Cnsw_47"), ("Tour Singapore Speedway", "Cnsw_48")]) +
    _cup(20, [("Tour Athens Dash", "Cnsw_51"), ("GCN Daisy Cruiser", "Cnsw_52"),
              ("Wii Moonview Highway", "Cnsw_53"), ("Squeaky Clean Sprint", "Cnsw_54")]) +
    _cup(21, [("Tour Los Angeles Laps", "Cnsw_55"), ("GBA Sunset Wilds", "Cnsw_56"),
              ("Wii Koopa Cape", "Cnsw_57"), ("Tour Vancouver Velocity", "Cnsw_58")]) +
    _cup(22, [("Tour Rome Avanti", "Cnsw_61"), ("GCN DK Mountain", "Cnsw_62"),
              ("Wii Daisy Circuit", "Cnsw_63"), ("Piranha Plant Cove", "Cnsw_64")]) +
    _cup(23, [("Tour Madrid Drive", "Cnsw_65"), ("3DS Rosalina's Ice World", "Cnsw_66"),
              ("SNES Bowser Castle 3", "Cnsw_67"), ("Wii Rainbow Road", "Cnsw_68")])
)

COURSE_BY_NAME: dict[str, Course] = {c.name: c for c in COURSES}
COURSE_BY_INTERNAL: dict[str, Course] = {c.internal: c for c in COURSES if c.internal}

# ---------------------------------------------------------------------------
# Drivers. `internal` = mush::EDriverID::text_ / romfs Driver/<name>.szs stem.
# `default_id` = best-guess enum id (PTDV order on Wii U + Deluxe additions +
# BCP DrvChr01..08 appended). Numbering verified against RoGamer97's 4.0.0
# CPU Combo Modifier enum lists (2026-10-16); the runtime text_ tables are
# shifted by one (index 0 = Invalid) and are NOT used for ids any more.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Entry:
    name: str        # AP name
    internal: str    # in-game identifier
    default_id: int  # enum guess (see above)
    bcp: bool = False


def _entries(rows: list[tuple[str, str]], *, start: int = 0, bcp: bool = False) -> list[Entry]:
    return [Entry(n, i, start + k, bcp) for k, (n, i) in enumerate(rows)]


DRIVERS: list[Entry] = _entries([
    ("Mario", "Mario"), ("Luigi", "Luigi"), ("Peach", "Peach"), ("Daisy", "Daisy"),
    ("Yoshi", "Yoshi"), ("Toad", "Kinopio"), ("Toadette", "Kinopico"), ("Koopa Troopa", "Nokonoko"),
    ("Bowser", "Koopa"), ("Donkey Kong", "DK"), ("Wario", "Wario"), ("Waluigi", "Waluigi"),
    ("Rosalina", "Rosetta"), ("Metal Mario", "MetalMario"), ("Pink Gold Peach", "MetalPeach"),
    ("Lakitu", "Jugem"), ("Shy Guy", "Heyho"), ("Baby Mario", "BbMario"), ("Baby Luigi", "BbLuigi"),
    ("Baby Peach", "BbPeach"), ("Baby Daisy", "BbDaisy"), ("Baby Rosalina", "BbRosetta"),
    ("Larry", "Larry"), ("Lemmy", "Lemmy"), ("Wendy", "Wendy"), ("Ludwig", "Ludwig"),
    ("Iggy", "Iggy"), ("Roy", "Roy"), ("Morton", "Morton"), ("Mii", "Mii"),
    ("Tanooki Mario", "TanukiMario"), ("Link", "Link"), ("Villager", "AnimalBoyA"),
    ("Isabelle", "Shizue"), ("Cat Peach", "CatPeach"), ("Dry Bowser", "HoneKoopa"),
    ("Villager (Female)", "AnimalGirlA"),
    # Deluxe additions
    ("Gold Mario", "GoldMario"), ("Dry Bones", "Karon"), ("Bowser Jr.", "KoopaJr"),
    ("King Boo", "KingTeresa"), ("Inkling Girl", "InklingGirl"), ("Inkling Boy", "InklingBoy"),
    # EDriverID 43 is Link's Champion's Tunic form: a separate driver id in the
    # game (RoGamer97 4.0.0 CPU Combo Modifier: DRIVER_BOTW_LINK = 0x2B).
    ("Link (Champion's Tunic)", "LinkBotW"),
]) + _entries([
    # Booster Course Pass (DrvChr01..08 per the character layout tool's ui map)
    ("Birdo", "DrvChr01"), ("Kamek", "DrvChr02"), ("Petey Piranha", "DrvChr03"),
    ("Wiggler", "DrvChr04"), ("Diddy Kong", "DrvChr05"), ("Funky Kong", "DrvChr06"),
    ("Peachette", "DrvChr07"), ("Pauline", "DrvChr08"),
    ("Mii (Booster Course Pass)", "DrvMii"),  # EDriverID 52 (DRIVER_MII_BCP = 0x34)
], start=44, bcp=True)

BODIES: list[Entry] = _entries([
    ("Standard Kart", "K_Std"), ("Pipe Frame", "K_Skeleton"), ("Mach 8", "K_Sport"),
    ("Steel Driver", "K_Steel"), ("Cat Cruiser", "K_Cat"), ("Circuit Special", "K_Race"),
    ("Tri-Speeder", "K_Tri"), ("Badwagon", "K_Bad"), ("Prancer", "K_Prancer"),
    ("Biddybuggy", "K_Buggy"), ("Landship", "K_Landship"), ("Sneeker", "K_Sneeker"),
    ("Sports Coupe", "K_Coupe"), ("Gold Standard", "K_Gold"), ("Standard Bike", "B_Std"),
    ("Comet", "B_Comet"), ("Sport Bike", "B_Sport"), ("The Duke", "B_Duke"),
    ("Flame Rider", "B_Flame"), ("Varmint", "B_Varmint"), ("Mr. Scooty", "B_Scooty"),
    ("Jet Bike", "B_Jet"), ("Yoshi Bike", "B_Yoshi"), ("Standard ATV", "A_Std"),
    ("Wild Wiggler", "A_Wiggler"), ("Teddy Buggy", "A_Teddy"), ("GLA", "K_Gla"),
    ("W 25 Silver Arrow", "K_SilverArrow"), ("300 SL Roadster", "K_Roadster"),
    ("Blue Falcon", "K_BlueFalcon"), ("Tanooki Kart", "K_Tanooki"), ("B Dasher", "K_BDasher"),
    ("Master Cycle", "B_MasterCycle"), ("Streetle", "K_Streetle"), ("P-Wing", "K_PWing"),
    ("City Tripper", "B_CityTripper"), ("Bone Rattler", "A_BoneRattler"),
    ("Koopa Clown", "K_KoopaClown"), ("Splat Buggy", "A_SplatBuggy"), ("Inkstriker", "K_Inkstriker"),
    ("Master Cycle Zero", "B_MasterCycleZero"),
])

TIRES: list[Entry] = _entries([
    ("Standard", "T_Std"), ("Monster", "T_Monster"), ("Roller", "T_Roller"), ("Slim", "T_Slim"),
    ("Slick", "T_Slick"), ("Metal", "T_Metal"), ("Button", "T_Button"), ("Off-Road", "T_OffRoad"),
    ("Sponge", "T_Sponge"), ("Wood", "T_Wood"), ("Cushion", "T_Cushion"),
    ("Blue Standard", "T_BlueStd"), ("Hot Monster", "T_HotMonster"), ("Azure Roller", "T_AzureRoller"),
    ("Crimson Slim", "T_CrimsonSlim"), ("Cyber Slick", "T_CyberSlick"), ("Retro Off-Road", "T_RetroOffRoad"),
    ("Gold Tires", "T_Gold"), ("GLA Tires", "T_Gla"), ("Triforce Tires", "T_Triforce"),
    ("Leaf Tires", "T_Leaf"), ("Ancient Tires", "T_Ancient"),
])

GLIDERS: list[Entry] = _entries([
    ("Super Glider", "W_Std"), ("Cloud Glider", "W_Cloud"), ("Wario Wing", "W_Wario"),
    ("Waddle Wing", "W_Waddle"), ("Peach Parasol", "W_Parasol"), ("Parachute", "W_Parachute"),
    ("Parafoil", "W_Parafoil"), ("Flower Glider", "W_Flower"), ("Bowser Kite", "W_Kite"),
    ("Plane Glider", "W_Plane"), ("MKTV Parafoil", "W_MKTV"), ("Gold Glider", "W_Gold"),
    ("Hylian Kite", "W_Hylian"), ("Paper Glider", "W_Paper"), ("Paraglider", "W_Paraglider"),
])

# gear::EItemSlot order (bit i of the items mask)
ITEMS: list[Entry] = _entries([
    ("Banana", "Banana"), ("Green Shell", "KouraG"), ("Red Shell", "KouraR"), ("Mushroom", "Kinoko"),
    ("Bob-omb", "Bomhei"), ("Blooper", "Gesso"), ("Spiny Shell", "KouraB"), ("Triple Mushrooms", "Kinoko3"),
    ("Star", "Star"), ("Bullet Bill", "Killer"), ("Lightning", "Thunder"), ("Golden Mushroom", "KinokoP"),
    ("Fire Flower", "Fireball"), ("Piranha Plant", "Packun"), ("Boomerang Flower", "Boomerang"),
    ("Coin", "Coin"), ("Super Horn", "SHorn"), ("Triple Bananas", "Banana3"),
    ("Triple Green Shells", "KouraG3"), ("Triple Red Shells", "KouraR3"), ("Crazy Eight", "Eight"),
    ("Feather", "Feather"), ("Boo", "Teresa"),
])

# ---------------------------------------------------------------------------
# AP item / location naming helpers
# ---------------------------------------------------------------------------

LOCATION_TIERS = ("Finish", "Top 3", "Win")
PROGRESSIVE_SPEED = "Progressive Speed"
PROGRESSIVE_ENGINE = "Progressive Engine Class"
TROPHY = "Trophy"
FILLER = "Coin Bonus"

STARTING_DRIVER = "Mario"
STARTING_BODY = "Standard Kart"
STARTING_TIRE = "Standard"
STARTING_GLIDER = "Super Glider"
STARTING_ITEMS = ("Mushroom", "Banana", "Coin")
STARTING_CUP = "Mushroom Cup"
BCP_STARTING_CUP = "Golden Dash Cup"   # course_selection: booster_course_pass


def cup_item(cup: Cup) -> str:
    return cup.name


def cup_complete_location(cup: Cup) -> str:
    return f"Complete {cup.name} (Grand Prix)"


def course_location(course: Course, tier: str) -> str:
    return f"{course.name} - {tier}"


def driver_item(e: Entry) -> str:
    return f"Character: {e.name}"


def body_item(e: Entry) -> str:
    return f"Kart: {e.name}"


def tire_item(e: Entry) -> str:
    return f"Tires: {e.name}"


def glider_item(e: Entry) -> str:
    return f"Glider: {e.name}"


def item_item(e: Entry) -> str:
    return f"Item: {e.name}"


def normalize_internal(s: str) -> str:
    """Lower-case, strip common resource prefixes / suffixes so that
    "tc_Course_Gu_FirstCircuit", "Gu_FirstCircuit" and PlayReport
    "Gu_FirstCircuit" all compare equal."""
    s = (s or "").strip()
    for prefix in ("tc_Course_", "tc_Chara_", "Course_", "Driver_", "MenuDriver_"):
        if s.startswith(prefix):
            s = s[len(prefix):]
    for suffix in ("^l", "^s", ".png", ".szs", ".bars"):
        if s.endswith(suffix):
            s = s[: -len(suffix)]
    return s.lower()
