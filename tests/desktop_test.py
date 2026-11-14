import os, socket, subprocess, sys, tempfile, time
img = sys.argv[1] if len(sys.argv) > 1 else 'build/textile.img'
qemu = os.environ.get('QEMU', 'qemu-system-i386')
work = tempfile.mkdtemp()
sock = os.path.join(work, 'mon.sock')
log = os.path.join(work, 'serial.log')
proc = subprocess.Popen([qemu, '-drive', 'file=' + img + ',format=raw,if=floppy', '-display', 'none', '-no-reboot',
                         '-serial', 'file:' + log,
                         '-monitor', 'unix:' + sock + ',server,nowait'])
text = ''
try:
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)
    mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    mon.connect(sock)
    def cmd(c, wait=0.3):
        mon.sendall((c + '\n').encode())
        time.sleep(wait)
    time.sleep(7)
    # Exercise the shared desktop click dispatcher through keyboard activation.
    # QEMU's headless display does not provide a physical pointer event source.
    cmd('sendkey ret', 0.8)
    cmd('mouse_move -200 -200', 0.6)
    cmd('mouse_move 20 16', 0.6)
    cmd('mouse_button 1', 0.3)
    cmd('mouse_button 0', 0.8)
    cmd('sendkey tab', 0.5)
    cmd('sendkey 2', 0.8)
    for k in ['a', 's', 'd']:
        cmd('sendkey ' + k, 0.4)
    cmd('sendkey ctrl-alt-d', 1.5)
    for k in ['h', 'e', 'l', 'p']:
        cmd('sendkey ' + k, 0.3)
    cmd('sendkey ret', 1.0)
    time.sleep(1)
finally:
    proc.kill()
    try:
        text = open(log, errors='ignore').read()
    except Exception:
        pass
print(text[-1500:])
checks = [
 ('mouse ready', 'mouse: ps/2 ready' in text),
 ('desktop starts', 'desktop: start' in text),
 ('desktop activation reaches click handler', 'desktop: click' in text),
 ('start menu opens by keyboard', 'desktop: start menu open' in text),
 ('start menu selects an app', 'desktop: start menu select' in text and 'desktop: open piano' in text),
 ('piano keys by keyboard', 'piano: key' in text),
 ('esc returns to shell', 'desktop: exit' in text),
 ('help after esc', 'Commands:' in text.split('desktop: exit')[-1]),
 ('no panic', 'PANIC' not in text.upper()),
]
for n, ok in checks:
    print(('PASS ' if ok else 'FAIL ') + n)
if not all(ok for _, ok in checks):
    sys.exit(1)
