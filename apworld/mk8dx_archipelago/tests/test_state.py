from pathlib import Path

from client import tables
from client.learned import LearnedIds
from client.state import BridgeState


def test_state_is_vanilla_until_ap_slot_connected():
    st = BridgeState()
    m = st.build_state()
    assert not m.enabled and m.seq == 1
    st.ap_conn = "ready"
    assert st.build_state().enabled
    st.ap_conn = "disconnected"
    assert not st.build_state().enabled


def test_default_state_is_starting_loadout():
    st = BridgeState()
    st.ap_conn = "ready"
    m = st.build_state()
    assert m.enabled and m.seq == 1
    assert m.cups == 1  # Mushroom Cup only
    assert m.speed_pct == 75
    mario = next(e for e in tables.DRIVERS if e.name == "Mario")
    assert (m.drivers[mario.default_id // 32] >> (mario.default_id % 32)) & 1
    luigi = next(e for e in tables.DRIVERS if e.name == "Luigi")
    assert not (m.drivers[luigi.default_id // 32] >> (luigi.default_id % 32)) & 1
    coin = next(e for e in tables.ITEMS if e.name == "Coin")
    assert (m.items >> coin.default_id) & 1
    star = next(e for e in tables.ITEMS if e.name == "Star")
    assert not (m.items >> star.default_id) & 1
    assert m.def_driver == mario.default_id


def test_items_unlock_bits_and_speed():
    st = BridgeState()
    st.add_item("Flower Cup")
    st.add_item("Character: Luigi")
    st.add_item("Item: Star")
    st.add_item(tables.PROGRESSIVE_SPEED, 3)
    m = st.build_state()
    assert m.cups == 0b11
    luigi = next(e for e in tables.DRIVERS if e.name == "Luigi")
    assert (m.drivers[luigi.default_id // 32] >> (luigi.default_id % 32)) & 1
    star = next(e for e in tables.ITEMS if e.name == "Star")
    assert (m.items >> star.default_id) & 1
    assert m.speed_pct == 100
    st.received[tables.PROGRESSIVE_SPEED] = 1
    assert st.build_state().speed_pct == 83


def test_stat_cap_mode_rises_with_progressive_speed():
    st = BridgeState()
    st.apply_slot_data({"speed_restriction": 0, "speed_stages": 3, "starting_stat_cap": 8})
    m = st.build_state()
    assert m.stat_cap == 8
    assert m.speed_pct == 100          # the top-speed governor is off in this mode
    assert m.base_speed_pct == 75
    st.add_item(tables.PROGRESSIVE_SPEED)
    assert st.build_state().stat_cap == 12
    assert st.build_state().base_speed_pct == 83
    st.add_item(tables.PROGRESSIVE_SPEED, 2)
    assert st.build_state().stat_cap == 20
    assert st.build_state().base_speed_pct == 100
    st.add_item(tables.PROGRESSIVE_SPEED)   # extra copies do not overflow
    assert st.build_state().stat_cap == 20


def test_stat_cap_off_in_top_speed_mode_and_old_seeds():
    st = BridgeState()
    st.apply_slot_data({"speed_stages": 3})      # pre-0.3.0 seed: no speed_restriction key
    m = st.build_state()
    assert m.stat_cap == 20 and m.speed_pct == 75 and m.base_speed_pct == 100
    st.apply_slot_data({"speed_restriction": 0, "speed_stages": 0})
    m = st.build_state()
    assert m.stat_cap == 20 and m.speed_pct == 100 and m.base_speed_pct == 100


def test_stat_cap_is_on_the_wire():
    from client.protocol import encode
    st = BridgeState()
    st.apply_slot_data({"speed_restriction": 0, "starting_stat_cap": 5})
    assert b'"stat_cap":5' in encode(st.build_state())


def test_slot_data_shuffle_off_allows_everything():
    st = BridgeState()
    st.apply_slot_data({"shuffle_characters": False, "shuffle_items": False, "speed_stages": 0})
    m = st.build_state()
    assert m.items & ((1 << 23) - 1) == (1 << 23) - 1
    assert m.speed_pct == 100
    for e in tables.DRIVERS:
        assert (m.drivers[e.default_id // 32] >> (e.default_id % 32)) & 1


def test_runtime_part_tables_do_not_override_default_ids(tmp_path: Path):
    L = LearnedIds(tmp_path / "ids.json")
    # The game's text_ tables are shifted (index 0 = Invalid) and must not
    # move Luigi off his verified enum id of 1.
    assert L.learn_table("driver", [{"id": 40, "text": "Luigi"}, {"id": 0, "text": "Invalid"}]) == 0
    st = BridgeState(L)
    st.ap_conn = "ready"
    st.add_item("Character: Luigi")
    m = st.build_state()
    assert (m.drivers[1 // 32] >> (1 % 32)) & 1
    assert not (m.drivers[40 // 32] >> (40 % 32)) & 1
    # courses are still learned and persisted
    L2 = LearnedIds(tmp_path / "ids.json")
    assert L2.learn_course(3, "tc_Course_Gu_Cake")
    assert L2.course_for_id(3).name == "Sweet Sweet Canyon"


def test_bcp_only_starting_cup_from_slot_data():
    st = BridgeState()
    st.ap_conn = "ready"
    st.apply_slot_data({"starting_cup": tables.BCP_STARTING_CUP})
    golden_dash = next(c for c in tables.CUPS if c.name == tables.BCP_STARTING_CUP)
    assert st.build_state().cups == 1 << golden_dash.index  # Mushroom Cup stays locked
