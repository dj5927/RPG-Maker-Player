"""Install the public v1.4-to-v1.6 update over a fake user installation."""
from pathlib import Path
import importlib.util
import json
from tempfile import TemporaryDirectory
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OTA = ROOT / 'DIST/RPG_Maker_Player_SteamOS_Update_V1.6.tar.gz'
PREVIOUS = ROOT / 'work/v16_cleanup_source_backups/verified_profiles.json'
assert OTA.is_file() and PREVIOUS.is_file()

with TemporaryDirectory(prefix='rpgmp-steam-v16-upgrade-') as temp:
    player = Path(temp) / 'rpg maker player'
    player.mkdir()
    registry = player / 'runtime/_compat/patches.json'
    registry.parent.mkdir(parents=True)
    previous = json.loads(PREVIOUS.read_text(encoding='utf-8'))
    previous['enabled'] = False
    previous['games']['another_game'] = {
        'engine': 'VX', 'enabled': False, 'fingerprints': [], 'patches': []}
    previous['games']['curse_of_pleasure_v1']['patches'][0]['enabled'] = False
    registry.write_text(json.dumps(previous, ensure_ascii=False), encoding='utf-8')
    before = registry.read_bytes()
    user_files = {
        'game/My Game/Save01.rvdata': b'GAME_SAVE_KEPT',
        'runtime/rtp/xp/Audio/BGM/MyTheme.mid': b'RTP_KEPT',
        'runtime/rtp/fonts/MyFont.ttf': b'CUSTOM_FONT_KEPT',
        'config/launcher.json': b'USER_CONFIG',
    }
    for relative, content in user_files.items():
        path = player / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    with tarfile.open(OTA, 'r:gz') as archive:
        names = set(archive.getnames())
        assert 'runtime/_compat/patches.json' not in names
        assert all(not n.startswith(('game/', 'config/', 'cache/')) for n in names)
        for member in archive:
            assert member.isfile() and '..' not in Path(member.name).parts
            out = player / member.name
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(archive.extractfile(member).read())
    assert registry.read_bytes() == before
    for relative, content in user_files.items():
        assert (player / relative).read_bytes() == content
    assert (player / 'VERSION').read_text().strip() == '1.6'

    location = player / 'runtime/_compat/merge_registry.py'
    spec = importlib.util.spec_from_file_location('release_registry', location)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.merge_registry(player, player / 'game')
    merged = module.read_registry(registry)
    assert not merged['enabled']
    assert not merged['games']['another_game']['enabled']
    assert not merged['games']['curse_of_pleasure_v1']['patches'][0]['enabled']
    assert len(merged['games']['curse_of_pleasure_v1']['patches']) == 4
    assert (registry.parent / 'patches_before_v16_cleanup.json').read_bytes() == before
    for relative, content in user_files.items():
        assert (player / relative).read_bytes() == content

print('STEAMOS_V16_PUBLIC_V14_OTA_UPGRADE_USER_DATA_AND_FOUR_RULES_PASS')
