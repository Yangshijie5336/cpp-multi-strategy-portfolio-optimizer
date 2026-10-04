from pathlib import Path
import urllib.request,json,zipfile,sys
root=Path(__file__).resolve().parents[1]
target=root/'.deps/python'
target.mkdir(parents=True,exist_ok=True)
for name,version in [('numpy','2.2.6'),('scipy','1.14.1'),('xlrd','2.0.2')]:
    if (target/name/'__init__.py').exists(): continue
    meta=json.load(urllib.request.urlopen(f'https://pypi.org/pypi/{name}/{version}/json'))
    wheels=[a for a in meta['urls'] if a['filename'].endswith('.whl') and ('cp312-cp312-win_amd64' in a['filename'] or 'py2.py3-none-any' in a['filename'])]
    if not wheels: raise RuntimeError(name)
    wheel=root/'.deps'/wheels[0]['filename']
    urllib.request.urlretrieve(wheels[0]['url'],wheel)
    with zipfile.ZipFile(wheel) as z:z.extractall(target)
sys.path.insert(0,str(target))
import numpy,scipy,pandas,xlrd
print(numpy.__version__,scipy.__version__,pandas.__version__,xlrd.__version__)
