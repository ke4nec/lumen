import os,subprocess,time
from pathlib import Path
import dbus

root=Path('/home/niq/prj/lumen')
session=dbus.SessionBus()
address=session.get_object('org.a11y.Bus','/org/a11y/bus').GetAddress(dbus_interface='org.a11y.Bus')
env=dict(os.environ,AT_SPI_BUS_ADDRESS=str(address),ATSPI_USE_LEGACY_DEVICE='1')
log=Path('/tmp/lumen-orca-designer-trace-speech.log')
startup=Path('/tmp/lumen-orca-designer-trace-startup.log')
protocol=Path('/tmp/lumen-orca-designer-trace-protocol.log')
for path in (log,startup,protocol): path.write_bytes(b'')
with startup.open('wb') as stream:
    reader=subprocess.Popen(['/usr/bin/python3','/tmp/lumen-orca-speech-monitor-trace.py'],env=dict(env,NO_AT_BRIDGE='1'),stdout=stream,stderr=subprocess.STDOUT,cwd=root)
    try:
        time.sleep(6)
        with protocol.open('wb') as output:
            result=subprocess.run(['/usr/bin/python3','-B','/tmp/lumen-designer-orca-sensitive-fixed.py','build-debug-a11y-ninja/examples/designer/lumen-designer','tests/fixtures/designer_atspi_window.design','linux-x11'],env=dict(env,SDL_VIDEODRIVER='x11'),cwd=root,stdout=output,stderr=subprocess.STDOUT,timeout=660)
        print('protocol exit',result.returncode,'reader active',reader.poll() is None,flush=True)
        print(protocol.read_text(),flush=True)
    finally:
        if reader.poll() is None: reader.terminate()
        try: reader.wait(timeout=8)
        except subprocess.TimeoutExpired:
            reader.kill()
            reader.wait(timeout=5)
lines=log.read_text().splitlines()
speech=[line for line in lines if 'SPEECH OUTPUT:' in line]
print('speech entries',len(speech),flush=True)
for line in speech[:30]: print(line,flush=True)
print('Designer debug entries',sum('Lumen Designer' in line for line in lines),flush=True)
