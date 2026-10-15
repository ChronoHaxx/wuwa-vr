#!/usr/bin/env python3
"""The in-game steps card's plan file (<profile>/wuwa-steps/plan.json), checked the way the plugin reads it.

    python dev/steps-plan.py check PLAN.json        validate only
    python dev/steps-plan.py install PLAN.json      validate, archive the current plan, install this one
    python dev/steps-plan.py archive                end the test: plan.json -> plan.<id>.json (the card goes away)
    python dev/steps-plan.py answers [PLAN_ID]      the answers recorded for a plan (default: the newest plan)

The rules follow mod/uevr/src/utility/WuWaStepPlan.hpp: a rejected plan shows no card at all, so a plan is
checked here first. The game loads a new plan within a second and logs "plan_loaded" to results.jsonl;
archiving restores any setting a step changed. Players never get a plan; only test sessions write one.
"""
import json
import os
import re
import sys
from pathlib import Path

FOLDER = Path(os.environ.get('APPDATA', '')) / 'UnrealVRMod/Client-Win64-Shipping/wuwa-steps'
MAX_PLAN_BYTES, MAX_STEPS, MAX_ANSWERS, MAX_TEXT, MAX_LABEL, MAX_ID, MAX_SETTINGS = 64 * 1024, 60, 8, 600, 80, 64, 16
VALID_ID = re.compile(r'[A-Za-z0-9_.\-]+')


def problems(data: bytes) -> list[str]:
    """Every reason the plugin would refuse this plan (sizes are UTF-8 bytes, as in the plugin)."""
    if not 0 < len(data) <= MAX_PLAN_BYTES:
        return ['plan.json must be 1 byte to 64 KB']
    try:
        plan = json.loads(data.decode('utf-8'))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        return [f'not JSON: {error}']
    found = []

    def text(obj, key, limit, required, where):
        value = obj.get(key)
        if value is None:
            if required:
                found.append(f'{where}: "{key}" is missing')
            return ''
        if not isinstance(value, str):
            found.append(f'{where}: "{key}" must be text')
            return ''
        if len(value.encode('utf-8')) > limit:
            found.append(f'{where}: "{key}" is {len(value.encode("utf-8"))} bytes (at most {limit})')
        return value

    if not isinstance(plan, dict):
        return ['the plan must be an object']
    plan_id = text(plan, 'id', MAX_ID, True, 'plan')
    if plan_id and not VALID_ID.fullmatch(plan_id):
        found.append('plan: "id" may only use letters, digits, - _ .')
    text(plan, 'title', MAX_LABEL, True, 'plan')
    text(plan, 'scene', MAX_TEXT, False, 'plan')
    steps = plan.get('steps')
    if not isinstance(steps, list) or not steps:
        return found + ['"steps" must be a non-empty list']
    if len(steps) > MAX_STEPS:
        found.append(f'{len(steps)} steps (at most {MAX_STEPS})')
    seen = set()
    for n, step in enumerate(steps, 1):
        where = f'step {n}'
        if not isinstance(step, dict):
            found.append(f'{where} must be an object')
            continue
        step_id = text(step, 'id', MAX_ID, True, where)
        where = f'step {n} ({step_id})'
        if step_id and not VALID_ID.fullmatch(step_id):
            found.append(f'{where}: "id" may only use letters, digits, - _ .')
        if step_id in seen:
            found.append(f'{where}: duplicate id')
        seen.add(step_id)
        text(step, 'title', MAX_LABEL, True, where)
        text(step, 'do', MAX_TEXT, False, where)
        text(step, 'expect', MAX_TEXT, False, where)
        answers = step.get('answers', [])
        if not isinstance(answers, list) or len(answers) > MAX_ANSWERS:
            found.append(f'{where}: "answers" must be a list of at most {MAX_ANSWERS}')
        else:
            for a in answers:
                if not isinstance(a, str) or not a or len(a.encode('utf-8')) > MAX_LABEL:
                    found.append(f'{where}: answer {a!r} must be non-empty text of at most {MAX_LABEL} bytes')
        settings = step.get('settings', {})
        if not isinstance(settings, dict) or len(settings) > MAX_SETTINGS:
            found.append(f'{where}: "settings" must be an object of at most {MAX_SETTINGS} keys')
            continue
        for key, value in settings.items():
            if not VALID_ID.fullmatch(key):
                found.append(f'{where}: setting key {key!r} is not a UEVR config key')
            ok = isinstance(value, (bool, int, float)) or (isinstance(value, str) and len(value.encode('utf-8')) <= MAX_LABEL)
            if not ok:
                found.append(f'{where}: setting {key} must be true / false, a number or short text')
    return found


def check(path: Path) -> dict:
    data = path.read_bytes()
    found = problems(data)
    if found:
        sys.exit('refused (the card would not show):\n  ' + '\n  '.join(found))
    return json.loads(data.decode('utf-8'))


def archive_current() -> str | None:
    current = FOLDER / 'plan.json'
    if not current.exists():
        return None
    try:
        old_id = json.loads(current.read_text(encoding='utf-8'))['id']
    except (ValueError, KeyError):
        old_id = 'unreadable'
    target = FOLDER / f'plan.{old_id}.json'
    n = 2
    while target.exists():
        target = FOLDER / f'plan.{old_id}.{n}.json'
        n += 1
    current.rename(target)
    return target.name


def answers(plan_id: str | None) -> None:
    results = FOLDER / 'results.jsonl'
    rows = [json.loads(line) for line in results.read_text(encoding='utf-8').splitlines() if line.strip()]
    if plan_id is None:
        loaded = [r['plan'] for r in rows if r.get('event') == 'plan_loaded']
        if not loaded:
            sys.exit('no plan has been loaded yet')
        plan_id = loaded[-1]
    print('plan', plan_id)
    for r in rows:
        if r.get('plan') != plan_id:
            continue
        if r.get('event') == 'answer':
            note = f'  ({r["note"]})' if r.get('note') else ''
            print(f'  {r.get("index")}. {r.get("step")}: {r.get("answer")}{note}')
        elif r.get('event') in ('plan_loaded', 'plan_finished'):
            print(f'  [{r["event"]}]')


def main() -> None:
    args = sys.argv[1:]
    if not args or args[0] not in ('check', 'install', 'archive', 'answers'):
        sys.exit(__doc__)
    command = args[0]
    if command == 'check':
        plan = check(Path(args[1]))
        print(f'ok: {plan["id"]}, {len(plan["steps"])} steps')
    elif command == 'install':
        plan = check(Path(args[1]))
        FOLDER.mkdir(parents=True, exist_ok=True)
        old = archive_current()
        (FOLDER / 'plan.json').write_bytes(Path(args[1]).read_bytes())
        print(f'installed {plan["id"]} ({len(plan["steps"])} steps)' + (f'; archived the previous plan as {old}' if old else ''))
        print('the game logs plan_loaded in wuwa-steps/results.jsonl within a second while it runs')
    elif command == 'archive':
        old = archive_current()
        print(f'archived as {old}' if old else 'no current plan')
    else:
        answers(args[1] if len(args) > 1 else None)


if __name__ == '__main__':
    main()
