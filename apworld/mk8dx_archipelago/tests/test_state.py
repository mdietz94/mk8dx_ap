from pathlib import Path

from client import tables
from client.learned import LearnedIds
from client.state import BridgeState


def test_default_state_is_starting_loadout():
    st = BridgeState()
    m = st.build_state()
    assert m.enabled and m.seq == 1
    assert m.cups == 1  # Mushroom Cup only
    assert m.speed_pct == 60
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
    assert st.build_state().speed_pct == 73


def test_slot_data_shuffle_off_allows_everything():
    st = BridgeState()
    st.apply_slot_data({"shuffle_characters": False, "shuffle_items": False, "speed_stages": 0})
    m = st.build_state()
    assert m.items & ((1 << 23) - 1) == (1 << 23) - 1
    assert m.speed_pct == 100
    for e in tables.DRIVERS:
        assert (m.drivers[e.default_id // 32] >> (e.default_id % 32)) & 1


def test_learned_ids_override_defaults(tmp_path: Path):
    L = LearnedIds(tmp_path / "ids.json")
    # Game says Luigi is id 40 (not the default 1).
    assert L.learn_table("driver", [{"id": 40, "text": "Luigi"}, {"id": 0, "text": "Mario"}]) == 2
    st = BridgeState(L)
    st.add_item("Character: Luigi")
    m = st.build_state()
    assert (m.drivers[40 // 32] >> (40 % 32)) & 1
    # persisted and reloaded
    L2 = LearnedIds(tmp_path / "ids.json")
    assert L2.maps["driver"].id_for("luigi") == 40
    assert L2.learn_course(3, "tc_Course_Gu_Cake")
    assert L2.course_for_id(3).name == "Sweet Sweet Canyon"
