from pathlib import Path
import shutil, urllib.request, tarfile, hashlib
root = Path(__file__).resolve().parents[1]
backup = root / '.verification' / 'before_convex_repair'
backup.mkdir(parents=True, exist_ok=True)
for name in ['portfolio.hpp','main.cpp','convex_solver.hpp','build.ps1','REPORT.md','mvo_msvc.exe']:
    if not (backup/name).exists(): shutil.copy2(root/name, backup/name)
deps=root/'.deps'
deps.mkdir(exist_ok=True)
other=Path('C:/Users/YSJ/Desktop/STL/cmake-build-relwithdebinfo-visual-studio/_deps')
for src,name in [(other/'eigen-src','eigen'),(root/'.verification/nlopt','nlopt')]:
    if not (deps/name).exists(): shutil.copytree(src,deps/name,ignore=shutil.ignore_patterns('.git'))
archive=deps/'highs.tar.gz'
if not archive.exists(): urllib.request.urlretrieve('https://codeload.github.com/ERGO-Code/HiGHS/tar.gz/refs/tags/v1.11.0',archive)
print('HiGHS archive SHA256',hashlib.sha256(archive.read_bytes()).hexdigest())
if not (deps/'HiGHS-1.11.0').exists():
    with tarfile.open(archive) as t: t.extractall(deps,filter='data')
import sys
sys.path.insert(0,str(root/'.verification/python'))
import numpy, scipy, pandas, xlrd
print('Python',sys.version,'numpy',numpy.__version__,'scipy',scipy.__version__,'pandas',pandas.__version__)
