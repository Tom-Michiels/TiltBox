"""Compile the actual embed interpreter on the host; HAL calls are explicit mocks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];mp=root/'components/micropython/src'
with tempfile.TemporaryDirectory(prefix='tiltbox-interpreter-') as d:
    exe=Path(d)/'interpreter-smoke'
    cmd=['cc','-O1','-fno-strict-aliasing','-Wno-unused-function','-Wno-unused-parameter','-I'+str(root/'tests/stubs'),'-I'+str(mp),str(root/'tests/interpreter_smoke.c'),str(root/'main/embed_api.c')]+[str(p) for p in mp.rglob('*.c')]+['-lm','-o',str(exe)]
    subprocess.run(cmd,check=True)
    r=subprocess.run([str(exe)],text=True,capture_output=True,timeout=20,check=True)
    print(r.stdout)
    for expected in ['API_AND_GC_PASS','NO_FILESYSTEM_PASS','NO_HARDWARE_MODULE_PASS','VM_INTERRUPT_RETURNED','FRESH_INTERPRETER_PASS','SHOWS=2']:assert expected in r.stdout
    assert 'UNEXPECTED' not in r.stdout
    print('PASS: actual MicroPython interpreter, mock HAL; target Xtensa GC and electronics still need physical validation.')
