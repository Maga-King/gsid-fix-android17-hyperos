#!/usr/bin/env python3
"""Package only the late-activation module; never include the old GSID overlay."""
import hashlib
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parent
module = root / 'module'
(module / 'bin').mkdir(exist_ok=True)
if (root / 'build/gsid.android17.loopfix').is_file():
    shutil.copy2(root / 'build/gsid.android17.loopfix', module / 'bin/gsid')
files = ['module.prop', 'customize.sh', 'service.sh', 'rollback.sh', 'uninstall.sh',
         'bin/gsid', 'META-INF/com/google/android/update-binary',
         'META-INF/com/google/android/updater-script']
output = root / 'GSID-Fix-Android17-HyperOS-v17.1-local.zip'
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in files:
        data = (module / name).read_bytes()
        if name != 'bin/gsid': data = data.replace(b'\r\n', b'\n')
        info = zipfile.ZipInfo(name)
        info.create_system = 3
        mode = 0o755 if name.endswith('.sh') or name in ['bin/gsid', 'META-INF/com/google/android/update-binary'] else 0o644
        info.external_attr = (0o100000 | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, data)
    archive.writestr('skip_mount', '')
    archive.writestr('README-zh.md', (root / 'README-Android17.md').read_bytes())
    archive.writestr('LICENSE', (root / 'LICENSE').read_bytes())
    archive.writestr('patch-report.json', (root / 'patch-report.json').read_bytes())
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix('.zip.sha256').write_text(f'{digest}  {output.name}\n')
print(f'{output}\nSHA256 {digest}')
