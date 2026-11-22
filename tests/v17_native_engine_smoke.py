"""Actually run rebuilt MKXP-Z/PhysFS against a synthetic Linux game.

This reads no proprietary games, creates assets in Linux /tmp only, invokes
the three runtime executables under Xvfb/Mesa, and checks native resolution.
"""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib

ROOT = Path('/mnt/d/GPT/MKXP')


def png_file(path):
    def chunk(t, content):
        data = t + content
        return struct.pack('>I', len(content)) + data + struct.pack('>I', zlib.crc32(data) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress((b'\0' + b'\xff\0\0' * 2) * 2)) +
           chunk(b'IEND', b''))
    path.write_bytes(png)


CODE = r'''
begin
  tests = [
    ['graphics/pictures/test_avatar.png', 'GAME_LOCAL'],
    ['Graphics\\Pictures\\TEST_AVATAR.PNG', 'GAME_LOCAL'],
    ['graphics/pictures/shared.png', 'LOCAL_PRIORITY'],
    ['graphics/pictures/collision.png', 'LOCAL_CASE_PRIORITY'],
    ['Graphics/Pictures/duplicate.png', 'EXACT_LOWER_IN_SAME_MOUNT'],
    ['Graphics/Pictures/Duplicate.PNG', 'EXACT_UPPER_IN_SAME_MOUNT'],
    ['Graphics/Pictures/rtponly.png', 'RTP_IMAGE'],
    ['audio/bgm/towna.ogg', 'GAME_OGG'],
  ]
  tests.each do |name, marker|
    raise "FileTest.exists? #{name}" unless FileTest.exists?(name)
    raise "FileTest.file? #{name}" unless FileTest.file?(name)
    raise "File.exist? #{name}" unless File.exist?(name)
    raise "File.file? #{name}" unless File.file?(name)
    raise "System.exists? #{name}" unless System.rpgmp_resource_exists?(name)
    raise "System.file? #{name}" unless System.rpgmp_resource_file?(name)
    direct = System.rpgmp_resource_disk_path(name)
    raise "disk path missing #{name}" unless direct && File.file?(direct)
    data = File.open(name, 'rb') {|f|f.read}
    unless data.include?(marker)
      STDERR.puts '[CASEFOLD-COLLISION] requested=' + name +
        ' indexed=' + System.desensitize(name).to_s +
        ' resolved=' + direct.to_s + ' data=' + data[0, 60]
      raise "File.open marker #{name}"
    end
  end
  raise 'Dir.lookup' unless Dir.entries('graphics/pictures').include?('Test_Avatar.PNG')
  raise 'Dir.glob wrong casing' unless Dir.glob('graphics/pictures/*.png').include?('graphics/pictures/Test_Avatar.PNG')
  raise 'Dir.glob RTP union' unless Dir.glob('graphics/pictures/rtponly*.png').include?('graphics/pictures/RtpOnly.PNG')
  raise 'Dir.exist?' unless FileTest.directory?('graphics/pictures')
  raise 'Data.rxdata' unless FileTest.file?('data/scripts.RXDATA')
  raise 'Save was casefolded' if FileTest.exists?('saveslot.rxdata')
  raise 'Path traversal accepted' if System.rpgmp_resource_exists?('Graphics/../../etc/passwd')
  raise 'Absolute accepted' if System.rpgmp_resource_exists?('/etc/passwd')
  raise 'Unknown accepted' if FileTest.file?('graphics/pictures/missing.PNG')
  raise 'Symlink escaped mount root' if System.rpgmp_resource_disk_path('Graphics/Pictures/Escape.PNG')
  raise 'Picture load failed' unless Bitmap.new('graphics/pictures/test_avatar').width == 2
  STDERR.puts '[RGSS-NATIVE-CASEFOLD] PASS Ruby ' + RUBY_VERSION
rescue Exception => e
  STDERR.puts '[RGSS-NATIVE-CASEFOLD] FAIL ' + e.class.to_s + ': ' + e.message.to_s
ensure
  exit
end
'''

GENERATOR = r'''
require 'zlib'
source = File.open(ARGV[0], 'rb') {|f| f.read}
File.open('Data/Scripts.rxdata', 'wb') do |file|
  Marshal.dump([[1, 'CasefoldReadOnlySmoke', Zlib::Deflate.deflate(source)]], file)
end
'''


def main():
    for runtime in ('ruby18', 'ruby19', 'ruby31'):
        rgss_version, script_extension, engine_rtp = {
            'ruby18': (1, 'rxdata', 'xp'),
            'ruby19': (2, 'rvdata', 'vx'),
            'ruby31': (3, 'rvdata2', 'vxace'),
        }[runtime]
        with tempfile.TemporaryDirectory(prefix='rpgmp-casefold-'+runtime+'-') as directory:
            game = Path(directory) / 'game'
            rtp = Path(directory) / 'rtp'
            (game / 'Data').mkdir(parents=True)
            (game / 'Graphics/Pictures').mkdir(parents=True)
            (game / 'Audio/BGM').mkdir(parents=True)
            (rtp / engine_rtp / 'Graphics/Pictures').mkdir(parents=True)
            (game / 'Game.ini').write_text('[Game]\nTitle=CasefoldSmoke\nScripts=Data/Scripts.'+script_extension+'\n', encoding='utf-8')
            (game / 'mkxp.json').write_text('{"rgssVersion":'+str(rgss_version)+',"pathCache":true,"windowTitle":"CasefoldSmoke","fullscreen":false,"fixedAspectRatio":true}', encoding='utf-8')

            # Valid 2x2 PNG, plus separate readable sidecar text markers.
            png_file(game / 'Graphics/Pictures/Test_Avatar.PNG')
            (game / 'Graphics/Pictures/Shared.PNG').write_bytes(b'LOCAL_PRIORITY')
            (game / 'Graphics/Pictures/Collision.PNG').write_bytes(b'LOCAL_CASE_PRIORITY')
            (game / 'Graphics/Pictures/duplicate.png').write_bytes(b'EXACT_LOWER_IN_SAME_MOUNT')
            (game / 'Graphics/Pictures/Duplicate.PNG').write_bytes(b'EXACT_UPPER_IN_SAME_MOUNT')
            outside = Path(directory) / 'outside-secret.dat'
            outside.write_bytes(b'SHOULD_NOT_BE_EXPOSED_BY_CASEFOLD')
            (game / 'Graphics/Pictures/Escape.PNG').symlink_to(outside)
            (rtp / engine_rtp / 'Graphics/Pictures/collision.png').write_bytes(b'RTP_EXACT_BUT_LOWER_PRIORITY')
            (rtp / engine_rtp / 'Graphics/Pictures/RtpOnly.PNG').write_bytes(b'RTP_IMAGE')
            (game / 'Audio/BGM/TownA.ogg').write_bytes(b'GAME_OGG')
            (game / 'SaveSlot.rxdata').write_bytes(b'PLAYER_SAVE')
            # A valid PNG cannot contain the plain-text marker required by
            # the read test without appending it harmlessly after IEND.
            png = game / 'Graphics/Pictures/Test_Avatar.PNG'
            png.write_bytes(png.read_bytes() + b'GAME_LOCAL')

            generator = Path(directory) / 'generator.rb'
            script = Path(directory) / 'test_code.rb'
            generator.write_text(GENERATOR)
            script.write_text(CODE.replace('data/scripts.RXDATA', 'data/scripts.'+script_extension.upper()))
            runtime_path = ROOT / 'runtime' / runtime
            ruby = runtime_path / 'ruby/bin/ruby'
            gen = subprocess.run([str(ruby), str(generator), str(script)],
                                 cwd=game, text=True, capture_output=True,
                                 timeout=12)
            if gen.returncode != 0:
                raise RuntimeError('Failed generating test XP Scripts.rxdata for '+runtime+'\n'+gen.stderr)
            if script_extension != 'rxdata':
                (game / 'Data/Scripts.rxdata').rename(game / ('Data/Scripts.'+script_extension))

            env = dict(os.environ)
            ruby_paths = {
                'ruby18': [runtime_path / 'ruby/lib/ruby/1.8',
                           runtime_path / 'ruby/lib/ruby/1.8/x86_64-linux',
                           runtime_path / 'ruby/lib/ruby/site_ruby/1.8',
                           runtime_path / 'ruby/lib/ruby/site_ruby/1.8/x86_64-linux'],
                'ruby19': [runtime_path / 'nothreaded_ruby/lib/ruby/1.9.1',
                           runtime_path / 'nothreaded_ruby/lib/ruby/1.9.1/x86_64-linux',
                           runtime_path / 'nothreaded_ruby/lib/ruby/site_ruby/1.9.1',
                           runtime_path / 'nothreaded_ruby/lib/ruby/site_ruby/1.9.1/x86_64-linux'],
                'ruby31': [runtime_path / 'stdlib', runtime_path / 'stdlib/x86_64-linux'],
            }
            portable_load_path = ':'.join(str(p) for p in ruby_paths[runtime] if p.exists())
            library_dirs = [runtime_path, runtime_path / 'ruby/lib',
                            runtime_path / 'ruby/openssl/lib', ROOT / 'lib']
            if runtime == 'ruby19':
                library_dirs.insert(1, runtime_path / 'nothreaded_ruby/lib')
                library_dirs.insert(2, runtime_path / 'nothreaded_ruby/openssl/lib')
            env.update({
                'LD_LIBRARY_PATH': ':'.join(str(p) for p in library_dirs),
                'MKXP_PORTABLE_RUBY_LOADPATH': portable_load_path,
                'MKXP_PORTABLE_RTP_ROOT': str(rtp),
                'MKXP_PORTABLE_PRELOAD_DIR': str(ROOT / 'preload/common'),
                'RPGMP_RGSS_GAME_ROOT': str(game),
                'RPGMP_RGSS_ENGINE': {'xp': 'XP', 'vx': 'VX', 'vxace': 'VXACE'}[engine_rtp],
                'SDL_AUDIODRIVER': 'dummy',
                'ALSOFT_DRIVERS': 'null',
                'LIBGL_ALWAYS_SOFTWARE': '1',
                'SDL_VIDEODRIVER': 'x11',
            })
            if runtime == 'ruby19':
                # Match the REAL SteamOS launcher's long-established fix for
                # legacy 1.9 VM GC and tiny pthread stacks on modern Linux.
                env['RUBY_HEAP_MIN_SLOTS'] = '1000000'
                env['RUBY_GC_MALLOC_LIMIT'] = '1000000000'
                env['RUBY_FREE_MIN'] = '100000'
                pthread_compat = runtime_path / 'libpthread_retry_einval.so'
                if pthread_compat.is_file():
                    env['LD_PRELOAD'] = str(pthread_compat)
            soundfont = ROOT / 'assets/soundfonts/TimGM6mb.sf2'
            if soundfont.is_file():
                env['MKXP_PORTABLE_SOUNDFONT'] = str(soundfont)
            exe = Path(os.environ.get('RPGMP_NATIVE_TEST_ENGINE_ROOT', str(ROOT / 'runtime'))) / runtime / 'mkxp-z'
            try:
                result = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x720x24',
                                         str(exe)], cwd=game, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, errors='replace', timeout=38)
                print('NATIVE_ENGINE', runtime, 'return_code', result.returncode,
                      'log_lines', len(result.stdout.splitlines()), flush=True)
                print(result.stdout[-4500:], flush=True)
                if '[RGSS-NATIVE-CASEFOLD] PASS' not in result.stdout:
                    raise AssertionError('Real game-process casefold integration did not pass: '+runtime)
            except subprocess.TimeoutExpired as err:
                raise AssertionError('Real mkxp-z synthetic game timed out: '+runtime+'\n'+str(err.stdout)[-2300:])
    print('STEAMOS_V1152_REAL_MKXP_NATIVE_GAME_PROCESS_THREE_RUBIES_PASS', flush=True)


if __name__ == '__main__':
    main()
