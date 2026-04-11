from __future__ import annotations

import json
from pathlib import Path

from .simulator import build_case_from_dict


def load_case(path: str | Path):
    with Path(path).open("r", encoding="utf-8") as handle:
        raw_case = json.load(handle)
    return build_case_from_dict(raw_case)
