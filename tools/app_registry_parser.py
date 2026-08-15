#!/usr/bin/env python3
"""Parse the playable app registry and per-game metadata declarations."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


@dataclass(frozen=True)
class ScoreInfo:
    game_id: str
    label: str
    best_key: str
    unit: str
    lower_is_better: bool


@dataclass(frozen=True)
class AppInfo:
    index: int
    id: str
    title: str
    screen_title: str | None
    subtitle: str
    label: str
    blurb: str
    icon: str
    default_visible: bool
    metadata_function: str
    source_file: str
    score: ScoreInfo | None


@dataclass(frozen=True)
class SystemAppInfo:
    id: str
    title: str
    subtitle: str
    icon: str
    capabilities: str
    follows_layout: bool


def read(*parts: str) -> str:
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def split_fields(text: str) -> list[str]:
    fields = []
    current = []
    depth = 0
    in_string = False
    escape = False
    for ch in text:
        if in_string:
            current.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            current.append(ch)
            continue
        if ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        if ch == "," and depth == 0:
            fields.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        fields.append(tail)
    return fields


def parse_string(field: str) -> str | None:
    field = field.strip()
    if field == "nullptr":
        return None
    match = re.fullmatch(r'"([^"]*)"', field)
    if not match:
        raise ValueError("expected string literal or nullptr, got %r" % field)
    return match.group(1)


def parse_bool(field: str) -> bool:
    field = field.strip()
    if field == "true":
        return True
    if field == "false":
        return False
    raise ValueError("expected bool literal, got %r" % field)


def parse_icon(field: str) -> str:
    match = re.fullmatch(r"LauncherIcon::(\w+)", field.strip())
    if not match:
        raise ValueError("expected LauncherIcon::<Name>, got %r" % field)
    return match.group(1)


def parse_int(field: str) -> int:
    field = field.strip()
    if not re.fullmatch(r"\d+", field):
        raise ValueError("expected integer literal, got %r" % field)
    return int(field)


def parse_metadata_file(path: str) -> tuple[dict[str, dict], dict[str, str]]:
    text = read(path)
    score_defs = {}
    metadata_defs = {}

    for match in re.finditer(r"constexpr\s+AppScoreInfo\s+(\w+)\s*=\s*\{(.*?)\};", text, re.S):
        name = match.group(1)
        fields = split_fields(match.group(2))
        if len(fields) != 5:
            raise ValueError("%s: AppScoreInfo %s has %d fields" % (path, name, len(fields)))
        score_defs[name] = ScoreInfo(
            game_id=parse_string(fields[0]) or "",
            label=parse_string(fields[1]) or "",
            best_key=parse_string(fields[2]) or "",
            unit=parse_string(fields[3]) or "",
            lower_is_better=parse_bool(fields[4]),
        )

    for match in re.finditer(r"constexpr\s+AppMetadata\s+(\w+)\s*=\s*\{(.*?)\};", text, re.S):
        name = match.group(1)
        fields = split_fields(match.group(2))
        if len(fields) != 10:
            raise ValueError("%s: AppMetadata %s has %d fields" % (path, name, len(fields)))
        score = None
        score_ref = fields[6].strip()
        if score_ref != "nullptr":
            score_match = re.fullmatch(r"&(\w+)", score_ref)
            if not score_match or score_match.group(1) not in score_defs:
                raise ValueError("%s: AppMetadata %s has unknown score ref %r" % (path, name, score_ref))
            score = score_defs[score_match.group(1)]
        metadata_defs[name] = {
            "id": parse_string(fields[0]) or "",
            "title": parse_string(fields[1]) or "",
            "screen_title": parse_string(fields[2]),
            "subtitle": parse_string(fields[3]) or "",
            "label": parse_string(fields[4]) or "",
            "blurb": parse_string(fields[5]) or "",
            "score": score,
            "icon": parse_icon(fields[7]),
            "index": parse_int(fields[8]),
            "default_visible": parse_bool(fields[9]),
            "source_file": path,
        }

    functions = {}
    for match in re.finditer(r"const\s+AppMetadata&\s+(\w+)\s*\(\)\s*\{\s*return\s+(\w+);\s*\}", text):
        functions[match.group(1)] = match.group(2)
    return metadata_defs, functions


def playable_apps() -> list[AppInfo]:
    game_dir = os.path.join(ROOT, "src", "games")
    metadata_defs = {}
    function_to_metadata = {}
    for name in sorted(os.listdir(game_dir)):
        if not name.endswith(".cpp"):
            continue
        path = os.path.join("src", "games", name)
        defs, funcs = parse_metadata_file(path)
        metadata_defs.update(defs)
        function_to_metadata.update(funcs)

    registry = read("src", "engine", "AppRegistry.cpp")
    legacy = re.search(
        r"metadataCatalogApp\(\s*\d+\s*,\s*\w+\(\)\s*,\s*LauncherIcon::", registry
    )
    if legacy:
        raise ValueError(
            "AppRegistry.cpp still supplies launcher index/icon; those belong in AppMetadata"
        )
    apps = []
    for match in re.finditer(
        r"metadataCatalogApp\(\s*(\w+)\(\)\s*,", registry
    ):
        func = match.group(1)
        if func not in function_to_metadata:
            raise ValueError("AppRegistry.cpp refers to unknown metadata function %s()" % func)
        metadata_name = function_to_metadata[func]
        if metadata_name not in metadata_defs:
            raise ValueError("%s() returns unknown metadata %s" % (func, metadata_name))
        data = metadata_defs[metadata_name]
        apps.append(
            AppInfo(
                index=data["index"],
                id=data["id"],
                title=data["title"],
                screen_title=data["screen_title"],
                subtitle=data["subtitle"],
                label=data["label"],
                blurb=data["blurb"],
                icon=data["icon"],
                default_visible=data["default_visible"],
                metadata_function=func,
                source_file=data["source_file"],
                score=data["score"],
            )
        )
    apps.sort(key=lambda app: app.index)
    return apps


def system_apps() -> list[SystemAppInfo]:
    registry = read("src", "engine", "AppRegistry.cpp")
    if "const AppDefinition APP_REGISTRY" in registry:
        registry = registry.split("const AppDefinition APP_REGISTRY", 1)[1]
    apps = []
    for match in re.finditer(r"systemApp\((.*?)\),", registry, re.S):
        fields = split_fields(match.group(1))
        if len(fields) not in (6, 7):
            raise ValueError("AppRegistry.cpp: systemApp has %d fields" % len(fields))
        apps.append(
            SystemAppInfo(
                id=parse_string(fields[0]) or "",
                title=parse_string(fields[1]) or "",
                subtitle=parse_string(fields[2]) or "",
                icon=parse_icon(fields[3]),
                capabilities=fields[5].strip(),
                follows_layout=parse_bool(fields[6]) if len(fields) == 7 else False,
            )
        )
    return apps
