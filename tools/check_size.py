import os, sys
path = sys.argv[1]
max_bytes = int(sys.argv[2])
sz = os.path.getsize(path)
if sz > max_bytes:
    sys.stderr.write(f"{path}: {sz} bytes exceeds limit {max_bytes}\n")
    sys.exit(1)
if len(sys.argv) > 3:
    pad = int(sys.argv[3])
    if pad > 0 and sz < pad:
        with open(path, "r+b") as fp:
            fp.truncate(pad)
