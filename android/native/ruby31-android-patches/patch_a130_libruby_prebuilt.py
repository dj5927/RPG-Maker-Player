from pathlib import Path
p=Path('/opt/rpgmp-mkxpz-build/app/jni/build-arm64-v8a/lib/libruby.so.3.1.3')
data=bytearray(p.read_bytes())
for off in (0x62d200,0x62d210):
    old=bytes(data[off:off+4])
    if old != bytes.fromhex('40008052'):
        raise SystemExit(f'unexpected bytes at {off:#x}: {old.hex()}')
    data[off:off+4]=bytes.fromhex('20008052')
p.write_bytes(data)
print('PATCHED_PREBUILT_LOCALE_UTF8')
