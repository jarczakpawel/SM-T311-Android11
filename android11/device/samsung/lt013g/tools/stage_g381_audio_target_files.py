#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from xml.etree import ElementTree
from zipfile import ZipFile

BOOT_SHA = 'cad5e76faf372beff3243ec615924286096713c994a6e5a03d3231f69478e6fd'
RECOVERY_SHA = 'f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f'
RIL_SHA = '719750b0f544238c0986ecbbc8c7d4b59b822f00c7ba1c883809b1b54fcee491'
AUDIO_SHA = '9debeb1023a86cfa9aa54775f9d3a12d0c9c0ac2c62c037852306811b54b8a59'
FSTAB_ENTRY = 'RECOVERY/RAMDISK/etc/recovery.fstab'
ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
if args.source.resolve() == args.output.resolve() or args.output.exists():
    raise SystemExit('Refusing to overwrite an existing target-files archive')

expected = {
    'IMAGES/boot.img': BOOT_SHA,
    'IMAGES/recovery.img': RECOVERY_SHA,
    'SYSTEM/vendor/lib/libril.so': RIL_SHA,
    'SYSTEM/lib/libaudiohal.so': AUDIO_SHA,
}
with ZipFile(args.source) as source:
    for name, digest in expected.items():
        if hashlib.sha256(source.read(name)).hexdigest() != digest:
            raise SystemExit('Unexpected release component: ' + name)
    if FSTAB_ENTRY in source.namelist():
        raise SystemExit('Recovery fstab already exists; use original target-files')
    manifest = ElementTree.fromstring(source.read('SYSTEM/vendor/etc/vintf/manifest.xml'))
    for service, interface in (('android.hardware.audio', 'IDevicesFactory'),
                               ('android.hardware.audio.effect', 'IEffectsFactory')):
        if not any(hal.get('format') == 'hidl' and hal.findtext('name') == service and
                   hal.findtext('version') == '6.0' and
                   any(item.findtext('name') == interface and
                       item.findtext('instance') == 'default' for item in hal.findall('interface'))
                   for hal in manifest.findall('hal')):
            raise SystemExit('Audio HIDL service missing: ' + service)

fstab = (ROOT / 'rootdir/fstab.smdk4x12').read_bytes()
for mount in (b'/system', b'/data', b'/cache', b'/boot', b'/recovery'):
    if not any(mount == line.split()[1] for line in fstab.splitlines()
               if line.strip() and not line.lstrip().startswith(b'#') and len(line.split()) >= 2):
        raise SystemExit('Missing recovery partition mapping: ' + mount.decode())

args.output.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(args.source, args.output)
with ZipFile(args.output, 'a') as staged:
    staged.writestr(FSTAB_ENTRY, fstab)
with ZipFile(args.output) as staged:
    if staged.read(FSTAB_ENTRY) != fstab or staged.testzip() is not None:
        raise SystemExit('Staged target-files validation failed')

result = {'status': 'G381_TARGET_FILES_READY',
          'archive': str(args.output),
          'sha256': hashlib.file_digest(args.output.open('rb'), 'sha256').hexdigest(),
          'bytes': args.output.stat().st_size,
          'boot_sha256': BOOT_SHA, 'recovery_sha256': RECOVERY_SHA,
          'ril_sha256': RIL_SHA, 'audio_sha256': AUDIO_SHA,
          'audio_hidl_6_0': True, 'recovery_fstab_staged': True}
args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
