import concurrent.futures
import pathlib
import urllib.request
import base64

ROOT = pathlib.Path(__file__).parent / 'reference'
URLS = {
    'gsi_service.cpp': 'https://raw.githubusercontent.com/LineageOS/android_system_gsid/lineage-23.0/gsi_service.cpp',
    'gsi_tool.cpp': 'https://raw.githubusercontent.com/LineageOS/android_system_gsid/lineage-23.0/gsi_tool.cpp',
    'gsid.cpp': 'https://raw.githubusercontent.com/LineageOS/android_system_gsid/lineage-23.0/gsid.cpp',
    'image_manager.cpp': 'https://raw.githubusercontent.com/LineageOS/android_system_core/lineage-23.0/fs_mgr/libfiemap/image_manager.cpp',
    'IGsiService.aidl': 'https://raw.githubusercontent.com/LineageOS/android_system_gsid/lineage-23.0/aidl/android/gsi/IGsiService.aidl',
}
def fetch(item):
    name, url = item
    try:
        if name != 'image_manager.cpp':
            path = 'aidl/android/gsi/IGsiService.aidl' if name.endswith('.aidl') else name
            url = 'https://android.googlesource.com/platform/system/gsid/+/refs/heads/main/' + path + '?format=TEXT'
            data = base64.b64decode(urllib.request.urlopen(url, timeout=30).read())
        else:
            data = urllib.request.urlopen(url, timeout=30).read()
        (ROOT / name).write_bytes(data)
        return f'{name}: {len(data)} bytes'
    except Exception as e:
        return f'{name}: {e}'
ROOT.mkdir(exist_ok=True)
with concurrent.futures.ThreadPoolExecutor() as pool:
    for result in pool.map(fetch, URLS.items()):
        print(result)
