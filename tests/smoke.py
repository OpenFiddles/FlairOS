import os, re, socket, subprocess, sys, tempfile, time

img = sys.argv[1] if len(sys.argv) > 1 else 'build/textile.img'
qemu = os.environ.get('QEMU', 'qemu-system-i386')
work = tempfile.mkdtemp()

disk = os.path.join(work, 'storage-30mb.img')
with open(disk, 'wb') as f:
    f.truncate(30 * 1024 * 1024)

keys = {' ': 'spc', '\n': 'ret'}


def wait_for(log, marker, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data = open(log, errors='ignore').read()
        except FileNotFoundError:
            data = ''
        if marker in data:
            return data
        time.sleep(0.1)
    raise RuntimeError('timed out waiting for serial marker: ' + marker)


def boot(tag, cmds):
    sock = os.path.join(work, tag + '.sock')
    log = os.path.join(work, tag + '.log')
    proc = subprocess.Popen([qemu, '-fda', img, '-hda', disk, '-display', 'none', '-no-reboot',
                             '-netdev', 'user,id=n0',
                             '-device', 'rtl8139,netdev=n0',
                             '-serial', 'file:' + log,
                             '-monitor', 'unix:' + sock + ',server,nowait'])
    try:
        for _ in range(100):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        mon.connect(sock)

        # The kernel now boots directly into the desktop. Wait for that state
        # before asking it to switch to the developer shell; a fixed delay can
        # send the shortcut during the VBE splash and leave it pending.
        wait_for(log, 'display: AirGraph pixel desktop active')
        wait_for(log, 'desktop: start')

        def send_key(key, delay=0.2):
            mon.sendall(('sendkey %s\n' % key).encode())
            time.sleep(delay)

        # Exercise the built-in .airp apps before entering the developer shell.
        send_key('tab')
        send_key('6')
        wait_for(log, 'desktop: open settings')
        time.sleep(0.4)
        send_key('esc', 0.6)
        send_key('tab')
        send_key('7')
        wait_for(log, 'desktop: open notes')
        time.sleep(0.4)
        send_key('h')
        send_key('i')
        send_key('ctrl-s', 0.4)
        send_key('esc', 0.6)

        mon.sendall(b'sendkey ctrl-alt-d\n')
        wait_for(log, 'desktop: open terminal')

        def type_line(text):
            for ch in text + '\n':
                mon.sendall(('sendkey %s\n' % keys.get(ch, ch)).encode())
                time.sleep(0.12)
            time.sleep(0.8)

        for cmd in cmds:
            type_line(cmd)
        time.sleep(4)
        mon.close()
        return open(log, errors='ignore').read()
    finally:
        proc.kill()


text = boot('first', ['help', 'time', 'uptime', 'in 60', 'echo hello textile', 'sqr 10 5', 'net',
                      'beep 523 50', 'mem', 'format', 'save note persistok', 'run'])

with open(disk, 'rb') as f:
    head = f.read(398 * 512 + 4)
fs_on_disk = head[398 * 512:398 * 512 + 4] == bytes.fromhex('59 44 53 46')
disk_is_30mb = os.path.getsize(disk) == 30 * 1024 * 1024

text2 = boot('reboot', ['load note'])

checks = [
    ('kernel banner', 'FlareOS 0.3'),
    ('AirGraph pixel desktop initializes', 'display: AirGraph pixel desktop active'),
    ('desktop never falls back to VGA', 'VGA recovery mode active' not in text),
    ('Settings AIRP launches', 'desktop: open settings'),
    ('Notes AIRP launches', 'desktop: open notes'),
    ('desktop boots first', 'desktop: start'),
    ('no kernel panic', 'PANIC' not in text.upper()),
    ('ctrl+alt+d opens the desktop Terminal', 'desktop: open terminal'),
    ('help list', 'Commands:'),
    ('help lists net', 'net            show'),
    ('help lists beep', 'beep HZ MS'),
    ('clock', re.compile(r'20\d\d-\d\d-\d\d \d\d:\d\d:\d\d')),
    ('uptime from timer', re.compile(r'up \d+ s')),
    ('port read', re.compile(r'port 0x0060 = 0x[0-9A-F]{2}')),
    ('echo', 'hello textile'),
    ('sqr draws square', 'square at 10 5'),
    ('net reports generic interface', re.compile(r'rtl8139\s+mac (?:[0-9A-F]{2}:){5}[0-9A-F]{2}\s+mtu 1500\s+link (?:up|down)')),
    ('net mac', re.compile(r'mac (?:[0-9A-F]{2}:){5}[0-9A-F]{2}')),
    ('net link state', re.compile(r'link (up|down)')),
    ('beep command', 'beep 523 Hz'),
    ('pmm reads e820 map', re.compile(r'pmm: e820 regions [1-9]')),
    ('pmm free memory', re.compile(r'pmm: \d+ MB free of \d+ MB managed')),
    ('pmm selftest', 'pmm: selftest passed'),
    ('kheap selftest', 'kheap: selftest passed'),
    ('mem command', re.compile(r'mem: \d+ MB free of \d+ MB managed')),
    ('paging on', 'vm: paging on, 4 GB identity map'),
    ('paging selftest', 'vm: selftest passed'),
    ('app space ready', 'vm: app space ready'),
    ('brk 1 MB heap', 'app: brk 1 MB ok'),
    ('app slide-in reaches target', 'app: slide-in done, title at col 35'),
    ('app chime', 'app: chime played'),
    ('app pulse', 'app: pulse done'),
    ('run replays the app', text.count('app: slide-in done') >= 2),
    ('ata detects blank disk', 'ata: primary disk ready'),
    ('format reports success', 'fs: disk formatted'),
    ('30 MB ATA storage image', disk_is_30mb),
    ('format writes FlareOS magic to disk', fs_on_disk),
    ('save reports success', 'fs: saved note'),
    ('reboot detects formatted disk', 'ata: primary disk ready'),
    ('file survives reboot', 'persistok' in text2),
]
ok = True
for name, pat in checks:
    if isinstance(pat, bool):
        found = pat
    elif hasattr(pat, 'search'):
        found = bool(pat.search(text if name != 'reboot detects formatted disk' else text2))
    else:
        found = pat in (text2 if name == 'reboot detects formatted disk' else text)
    ok = ok and found
    print(('PASS ' if found else 'FAIL ') + name)
print('--- serial tail (reboot) ---')
print(text2[-400:])
sys.exit(0 if ok else 1)
