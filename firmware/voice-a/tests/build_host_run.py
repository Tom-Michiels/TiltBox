"""Build tests/host_run (real MicroPython interpreter with a simulated TiltBox)."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1];mp=root/'components/micropython/src';out=root/'tests/host_run'
cmd=['cc','-O1','-fno-strict-aliasing','-Wno-unused-function','-Wno-unused-parameter','-I'+str(root/'tests/stubs'),'-I'+str(mp),str(root/'tests/host_run.c'),str(root/'main/embed_api.c')]+[str(p) for p in mp.rglob('*.c')]+['-lm','-o',str(out)]
subprocess.run(cmd,check=True);print(out)
