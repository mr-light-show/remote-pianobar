#!/usr/bin/env python3
"""
Compare overlapping locale keys across remote-pianobar, remote-pianobar-ha,
and remote_pianobar_card to detect English string drift.

Run from: remote-pianobar repo root
Adjacent repos expected at: ../remote-pianobar-ha and ../remote_pianobar_card

Exit 0  — all aligned concepts match (or the peer repo is absent)
Exit 1  — one or more mismatches detected
"""

import sys
import json
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML is required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------------------
# Curated concept map
# Each row is (player_dotkey, ha_dotkey_or_None, card_dotkey_or_None)
# describing the same user-facing English string in each catalog.
# "None" means the concept is not expressed in that repo yet.
# ---------------------------------------------------------------------------
ALIGNED_CONCEPTS = [
    # ---- universal UI verbs ----
    ("web.ui.cancel",                None,                      "common.cancel"),
    ("web.ui.save",                  None,                      "common.save"),
    ("web.ui.close",                 None,                      "station_info.close"),
    # ---- loading states ----
    ("web.ui.loading",               None,                      None),
    ("web.ui.loading_modes",         None,                      "station_mode.loading"),
    ("web.ui.loading_genres",        None,                      "create_station.loading_genres"),
    ("web.ui.loading_station_info",  None,                      "station_info.loading"),
    # ---- browse media ----
    ("web.media.my_stations",        "common.browse_media_my_stations",  None),
]


def load_player(path: Path) -> dict:
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def load_json(path: Path) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def get_nested(d: dict, dotkey: str):
    """Walk dot-separated keys; returns None if any segment is missing."""
    v = d
    for k in dotkey.split("."):
        if not isinstance(v, dict) or k not in v:
            return None
        v = v[k]
    return v if isinstance(v, str) else None


def check_pair(failures: list, label_a: str, key_a: str, val_a,
               label_b: str, key_b: str, val_b) -> None:
    if val_a is None or val_b is None:
        return
    if val_a != val_b:
        failures.append(
            f"  {label_a}[{key_a!r}] = {val_a!r}\n"
            f"  {label_b}[{key_b!r}] = {val_b!r}"
        )


def main() -> int:
    root = Path(__file__).parent.parent.resolve()

    # Player (YAML)
    player_path = root / "locale" / "en.yaml"
    if not player_path.exists():
        print(f"ERROR: player locale not found: {player_path}", file=sys.stderr)
        return 2
    player = load_player(player_path)

    # HA (JSON) — optional (sibling repo)
    ha_path = (root / ".." / "remote-pianobar-ha"
               / "custom_components" / "pianobar" / "translations" / "en.json")
    ha_common_path = None
    ha: dict = {}
    if ha_path.exists():
        raw = load_json(ha_path)
        # HA stores extra UI strings under "common" with slug keys
        ha = raw.get("common", {})
        ha_common_path = ha_path

    # Card (JSON) — optional (sibling repo)
    card_path = (root / ".." / "remote_pianobar_card"
                 / "src" / "translations" / "en.json")
    card: dict = {}
    if card_path.exists():
        card = load_json(card_path)

    failures: list = []

    for player_key, ha_key, card_key in ALIGNED_CONCEPTS:
        pv = get_nested(player, player_key)
        hv = get_nested(ha,     ha_key)  if ha_key  and ha   else None
        cv = get_nested(card,   card_key) if card_key and card else None

        check_pair(failures,
                   "player", player_key, pv,
                   "ha",     ha_key or "",   hv)
        check_pair(failures,
                   "player", player_key, pv,
                   "card",   card_key or "", cv)

    if failures:
        print("Locale sync FAILURES detected:")
        for entry in failures:
            print(entry)
        return 1

    repos_checked = ["player"]
    if ha_common_path:
        repos_checked.append("ha")
    if card_path.exists():
        repos_checked.append("card")
    print(f"Locale sync: OK ({', '.join(repos_checked)} checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
