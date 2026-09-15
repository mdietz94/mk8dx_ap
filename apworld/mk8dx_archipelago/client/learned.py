"""Runtime-learned id tables.

The public MK8DX headers leave the course / driver / vehicle enums empty, so
the mod reports what the game itself says:

* `id_table` messages: `mush::E{Driver,Body,Tire,Wing}ID::text_(id)` for
  every id -> internal name ("Kinopio", "K_Std", ...).
* `course_name` messages: `CourseInfo{mCourseId, mCourseIconResource}` for
  every course the player browses or starts.

`LearnedIds` merges those into the static defaults from tables.py and
persists them as JSON so a second session starts calibrated. Lookups go
through `IdMap.name_for(id)` / `IdMap.id_for(internal)`.
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

from . import tables

log = logging.getLogger("MK8DX")

KINDS = ("driver", "body", "tire", "wing", "course")


class IdMap:
    def __init__(self, kind: str, defaults: dict[int, str] | None = None):
        self.kind = kind
        self.id_to_internal: dict[int, str] = dict(defaults or {})
        self.learned: set[int] = set()

    def learn(self, id_: int, internal: str) -> bool:
        """Record an observation. Returns True when it changed anything."""
        if id_ < 0 or not internal:
            return False
        norm = tables.normalize_internal(internal)
        changed = self.id_to_internal.get(id_) != norm or id_ not in self.learned
        # A runtime observation supersedes any default-guess id that mapped
        # to the same internal name (e.g. defaults said Luigi=1, game says 40).
        for other, v in list(self.id_to_internal.items()):
            if other != id_ and v == norm and other not in self.learned:
                del self.id_to_internal[other]
        self.id_to_internal[id_] = norm
        self.learned.add(id_)
        return changed

    def internal_for(self, id_: int) -> str | None:
        return self.id_to_internal.get(id_)

    def id_for(self, internal: str) -> int | None:
        norm = tables.normalize_internal(internal)
        hit = None
        for k, v in self.id_to_internal.items():
            if v == norm:
                if k in self.learned:
                    return k
                if hit is None:
                    hit = k
        return hit

    def to_json(self) -> dict:
        return {str(k): v for k, v in self.id_to_internal.items() if k in self.learned}

    def from_json(self, data: dict) -> None:
        for k, v in (data or {}).items():
            try:
                self.learn(int(k), str(v))
            except (TypeError, ValueError):
                continue


def _defaults(entries: list[tables.Entry]) -> dict[int, str]:
    return {e.default_id: tables.normalize_internal(e.internal) for e in entries}


class LearnedIds:
    def __init__(self, path: Path | None = None):
        self.path = path
        self.maps: dict[str, IdMap] = {
            "driver": IdMap("driver", _defaults(tables.DRIVERS)),
            "body": IdMap("body", _defaults(tables.BODIES)),
            "tire": IdMap("tire", _defaults(tables.TIRES)),
            "wing": IdMap("wing", _defaults(tables.GLIDERS)),
            "course": IdMap("course"),
        }
        self._dirty = False
        if path is not None:
            self.load()

    # ---- persistence ----
    def load(self) -> None:
        if self.path is None or not self.path.exists():
            return
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except Exception as e:  # noqa: BLE001
            log.warning("learned ids: could not read %s (%s)", self.path, e)
            return
        for kind, m in self.maps.items():
            m.from_json(data.get(kind, {}))
        log.info("learned ids: loaded %s", {k: len(m.learned) for k, m in self.maps.items()})

    def save(self) -> None:
        if self.path is None or not self._dirty:
            return
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.path.with_suffix(".tmp")
            tmp.write_text(json.dumps({k: m.to_json() for k, m in self.maps.items()},
                                      indent=1, sort_keys=True), encoding="utf-8")
            tmp.replace(self.path)
            self._dirty = False
        except Exception as e:  # noqa: BLE001
            log.warning("learned ids: could not write %s (%s)", self.path, e)

    # ---- learning ----
    def learn_table(self, kind: str, entries: list[dict]) -> int:
        m = self.maps.get(kind)
        if m is None:
            log.warning("learned ids: unknown table kind %r", kind)
            return 0
        n = 0
        for e in entries:
            try:
                if m.learn(int(e.get("id", -1)), str(e.get("text", ""))):
                    n += 1
            except (TypeError, ValueError):
                continue
        if n:
            self._dirty = True
            self.save()
        return n

    def learn_course(self, course_id: int, res: str) -> bool:
        changed = self.maps["course"].learn(course_id, res)
        if changed:
            self._dirty = True
            self.save()
        return changed

    # ---- lookups ----
    def course_for_id(self, course_id: int) -> tables.Course | None:
        internal = self.maps["course"].internal_for(course_id)
        if internal is None:
            return None
        return course_for_internal(internal)

    def id_for_entry(self, kind: str, e: tables.Entry) -> int:
        """Enum id for a tables.Entry: learned if known, else the default guess."""
        found = self.maps[kind].id_for(e.internal)
        return found if found is not None else e.default_id


def course_for_internal(internal: str) -> tables.Course | None:
    """Match a game-reported course string against tables.COURSES.
    Accepts "tc_Course_Gu_FirstCircuit", "Gu_FirstCircuit", "Gu_FirstCircuit_x"."""
    norm = tables.normalize_internal(internal)
    for c in tables.COURSES:
        if not c.internal:
            continue
        ci = c.internal.lower()
        if norm == ci or norm.startswith(ci + "_") or norm.endswith("_" + ci) or ("_" + ci + "_") in norm:
            return c
    return None
