"""SteamOS 1.6: remove only the obsolete stock Avatar rule, keep user work."""
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
ANDROID = Path(r'D:\GPT\MKXP_ANDROID\app\src\main\assets\rgss_compat\verified_profiles.json')
VERIFIED = ROOT / 'runtime/_compat/verified_profiles.json'
OLD = ROOT / 'work/v16_cleanup_source_backups/verified_profiles.json'
spec = importlib.util.spec_from_file_location('steam_v16_compat', ROOT / 'runtime/_compat/merge_registry.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

stock = json.loads(VERIFIED.read_text(encoding='utf-8'))
previous = json.loads(OLD.read_text(encoding='utf-8'))
android = json.loads(ANDROID.read_text(encoding='utf-8'))
assert stock == android, 'Steam must bundle only Android four verified rules'
assert len(stock['games']['curse_of_pleasure_v1']['patches']) == 4
assert len(previous['games']['curse_of_pleasure_v1']['patches']) == 5
obsolete = previous['games']['curse_of_pleasure_v1']['patches'][-1]
assert obsolete['id'] == module.RETIRED_RULE_ID


def run(initial=None, expected=4, disabled=False, legacy=False):
    with TemporaryDirectory(prefix='rpgmp-v16-migration-') as d:
        base = Path(d)
        app = base / 'SteamOS Player'
        library = base / 'games'
        (app / 'runtime/_compat').mkdir(parents=True)
        library.mkdir()
        (app / 'runtime/_compat/verified_profiles.json').write_text(
            json.dumps(stock, ensure_ascii=False), encoding='utf-8')
        manifest = app / 'runtime/_compat/patches.json'
        old_location = library / '_compat/patches.json'
        target = old_location if legacy else manifest
        before = None
        if initial is not None:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(initial, ensure_ascii=False, indent=2), encoding='utf-8')
            before = target.read_bytes()
        module.merge_registry(app, library)
        result = module.read_registry(manifest)
        assert len(result['games']['curse_of_pleasure_v1']['patches']) == expected
        assert result['enabled'] is (not disabled)
        if initial is not None:
            assert target.read_bytes() == before if legacy else True
            assert result['games']['other_custom']['enabled'] is False
            if expected == 4 and not legacy:
                assert (manifest.parent / 'patches_before_v16_cleanup.json').is_file()
        newer = manifest.read_bytes()
        module.merge_registry(app, library)
        assert manifest.read_bytes() == newer, 'Repeated runs must be stable'
        return result


fresh = run()
assert '_rpgmp_migrations' not in fresh

old = json.loads(json.dumps(previous))
old['enabled'] = False
old['games']['other_custom'] = {
    'engine':'MV', 'enabled':False, 'fingerprints':[], 'patches':[]}
old['games']['curse_of_pleasure_v1']['patches'][0]['enabled'] = False
old['games']['curse_of_pleasure_v1']['patches'][-1]['enabled'] = False
old['_rpgmp_migrations'] = {'steamos_v1151_curse_avatar_casefold_offered': True}
retired = run(old, expected=4, disabled=True)
assert retired['games']['curse_of_pleasure_v1']['patches'][0]['enabled'] is False
assert '_rpgmp_migrations' not in retired
run(old, expected=4, disabled=True, legacy=True)

edited = json.loads(json.dumps(old))
edited['games']['curse_of_pleasure_v1']['patches'][-1]['replace'] += '\n# custom edit'
result = run(edited, expected=5, disabled=True)
assert result['games']['curse_of_pleasure_v1']['patches'][-1]['replace'].endswith('# custom edit')

other_fingerprint = json.loads(json.dumps(old))
other_fingerprint['games']['curse_of_pleasure_v1']['fingerprints'][0]['Data/System.rxdata'] = '0' * 64
run(other_fingerprint, expected=5, disabled=True)

print('STEAMOS_V16_FOUR_ONLY_FRESH_AND_FIVE_TO_FOUR_SAFE_USER_MIGRATION_PASS')
