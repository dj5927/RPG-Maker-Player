#!/usr/bin/env python3
"""Maintain one SteamOS runtime/_compat/patches.json without overwriting users.

Bundled Android-verified profiles are installed by content fingerprint; older
game-library and installation-root registries are imported once, non-destructively.
No games, archives, scripts or existing disabled profiles are modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

MAX_BYTES = 4 * 1024 * 1024

# Remove only the byte-for-byte obsolete stock rule, never user edits.
RETIRED_RULE_ID = 'curse-xp-avatar-filetest-casefold'
RETIRED_RULE_DIGEST = '16531a3ed506aaaa24da0f90a8455a4ee80418d84e99856bd3432c6e59af668b'
RETIRED_MIGRATION = 'steamos_v1151_curse_avatar_casefold_offered'


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key: ' + str(key))
        result[key] = value
    return result


def read_registry(path):
    if not path.is_file() or path.is_symlink():
        raise ValueError('expected a regular, non-symlink JSON file: ' + str(path))
    if path.stat().st_size > MAX_BYTES:
        raise ValueError('registry exceeds 4 MiB: ' + str(path))
    data = json.loads(path.read_text(encoding='utf-8-sig'),
                      object_pairs_hook=unique_pairs)
    if (not isinstance(data, dict) or data.get('schema') != 2 or
            type(data.get('enabled')) is not bool or
            not isinstance(data.get('games'), dict)):
        raise ValueError('expected schema=2 with boolean enabled and games object')
    if len(data['games']) > 1000:
        raise ValueError('too many profiles')
    return data


def save_atomic(path, data, original=None):
    # Save an immutable rollback copy of the original user-authored registry.
    if original is not None:
        backup = path.with_name('patches_before_verified_restore.json')
        if not backup.exists():
            with backup.open('xb') as target:
                target.write(original)
    staging = path.with_name('patches.json.mkxp-staging')
    # Refuse to interfere with an interrupted operation or a custom staging file.
    with staging.open('xb') as outfile:
        outfile.write((json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    os.replace(staging, path)


def retire_stock_casefix(registry, verified, changes):
    """Drop only the original generated Avatar rule for the original game.

    Preserve enabled flags, user-customized rules, and profiles for games with
    different content fingerprints. Re-running is always safe.
    """
    stock = verified.get('games', {}).get('curse_of_pleasure_v1')
    current = registry.get('games', {}).get('curse_of_pleasure_v1')
    if not isinstance(stock, dict) or not isinstance(current, dict):
        return
    if current.get('engine') != 'XP' or current.get('fingerprints') != stock.get('fingerprints'):
        return
    rules = current.get('patches')
    if not isinstance(rules, list):
        return
    for index in reversed(range(len(rules))):
        rule = rules[index]
        if not isinstance(rule, dict) or rule.get('id') != RETIRED_RULE_ID:
            continue
        if not isinstance(rule.get('enabled', True), bool):
            continue
        payload = {k: v for k, v in rule.items() if k != 'enabled'}
        canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True,
                               separators=(',', ':')).encode('utf-8')
        if hashlib.sha256(canonical).hexdigest() == RETIRED_RULE_DIGEST:
            rules.pop(index)
            changes.append('retired:' + RETIRED_RULE_ID)
    flags = registry.get('_rpgmp_migrations')
    if isinstance(flags, dict) and RETIRED_MIGRATION in flags:
        flags.pop(RETIRED_MIGRATION)
        if not flags:
            registry.pop('_rpgmp_migrations')
        changes.append('retired-marker:' + RETIRED_MIGRATION)


def merge_registry(player_root, game_library):
    destination = player_root / 'runtime/_compat/patches.json'
    bundled = player_root / 'runtime/_compat/verified_profiles.json'
    if not bundled.is_file():
        raise ValueError('bundled verified profiles missing')
    verified = read_registry(bundled)
    destination.parent.mkdir(parents=True, exist_ok=True)

    sources = (game_library / '_compat/patches.json',
               player_root / '_compat/patches.json')
    legacy = []
    for source in sources:
        if not source.exists():
            continue
        try:
            legacy.append((source, read_registry(source)))
        except (ValueError, OSError, json.JSONDecodeError) as error:
            print('[RPGMP-COMPAT] ignoring invalid legacy registry: ' + str(error),
                  file=sys.stderr)

    original = None
    if destination.exists():
        # Invalid user files must never be silently overwritten or discarded.
        original = destination.read_bytes()
        registry = read_registry(destination)
    elif legacy:
        # Preserve the old global enabled/disabled setting on first migration.
        registry = {'schema': 2, 'enabled': legacy[0][1]['enabled'], 'games': {}}
    else:
        registry = {'schema': 2, 'enabled': verified['enabled'], 'games': {}}

    changes = []
    for source, incoming in legacy:
        for key, profile in incoming['games'].items():
            if key not in registry['games']:
                registry['games'][key] = profile
                changes.append('legacy:' + key)

    for key, profile in verified['games'].items():
        if key not in registry['games']:
            registry['games'][key] = profile
            changes.append('verified:' + key)

    retire_stock_casefix(registry, verified, changes)

    if original is None or changes:
        if original is not None and any(item.startswith('retired') for item in changes):
            backup = destination.with_name('patches_before_v16_cleanup.json')
            if not backup.exists():
                with backup.open('xb') as output:
                    output.write(original)
        save_atomic(destination, registry, original)
        print('[RPGMP-COMPAT] runtime registry updated ' + str(destination) +
              ' new_profiles=' + ','.join(changes), flush=True)
    else:
        print('[RPGMP-COMPAT] runtime registry preserved ' + str(destination),
              flush=True)
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--player-root', required=True, type=Path)
    parser.add_argument('--game-library', required=True, type=Path)
    args = parser.parse_args()
    try:
        merge_registry(args.player_root, args.game_library)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print('[RPGMP-COMPAT] registry setup failed: ' + str(error),
              file=sys.stderr, flush=True)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
