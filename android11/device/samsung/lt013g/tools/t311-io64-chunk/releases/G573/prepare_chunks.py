import gzip
import hashlib
import json
import sys
from pathlib import Path

EXPECTED_IMAGE_SIZE = 2235564032
EXPECTED_IMAGE_SHA256 = "11c81fcf45dd86dacbdd85f521ccd6eb1cb8818c7c2bbdcee69bc0f113efa779"
EXPECTED_ZIP_SHA256 = "86ab028d7486f83548bfa8be538ea24ec1851323c602ab7d940f9f9003226864"
PART_SIZE = 536870912
TRANSFER = 1048576

def sha256_file(path):
    h = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(TRANSFER), b""):
            h.update(block)
    return h.hexdigest()

if len(sys.argv) != 4:
    sys.exit("usage: prepare_chunks.py RAW_IMAGE OUTPUT_DIR TARGETFILES_ZIP")
raw, dest, zip_path = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
if raw.stat().st_size != EXPECTED_IMAGE_SIZE:
    sys.exit("BLOCKED: raw image size does not match SM-T311 SYSTEM")
h = hashlib.sha256()
with zip_path.open("rb") as f:
    for b in iter(lambda: f.read(TRANSFER), b""):
        h.update(b)
if h.hexdigest() != EXPECTED_ZIP_SHA256:
    sys.exit("BLOCKED: source ROM package is not verified G562")
h = hashlib.sha256()
with raw.open("rb") as f:
    for b in iter(lambda: f.read(TRANSFER), b""):
        h.update(b)
if h.hexdigest() != EXPECTED_IMAGE_SHA256:
    sys.exit("BLOCKED: raw filesystem is not verified G562")

dest.mkdir(parents=True, exist_ok=True)
parts = []
with raw.open("rb") as source:
    for index in range(5):
        offset = index * PART_SIZE
        length = min(PART_SIZE, EXPECTED_IMAGE_SIZE - offset)
        gzip_path = dest / f"chunk{index}.raw.gz"
        source_hash = hashlib.sha256()
        if gzip_path.exists():
            left = length
            while left:
                data = source.read(min(TRANSFER, left))
                if not data:
                    sys.exit("BLOCKED: existing chunk source truncated")
                source_hash.update(data)
                left -= len(data)
        else:
            with gzip_path.open("wb") as output:
                with gzip.GzipFile(filename="", fileobj=output, mode="wb",
                                   compresslevel=1, mtime=0) as encoded:
                    left = length
                    while left:
                        data = source.read(min(TRANSFER, left))
                        if not data:
                            sys.exit("BLOCKED: raw chunk truncated")
                        left -= len(data)
                        source_hash.update(data)
                        encoded.write(data)
        check_hash = hashlib.sha256()
        count = 0
        with gzip.open(gzip_path, "rb") as verify:
            for data in iter(lambda: verify.read(TRANSFER), b""):
                count += len(data)
                check_hash.update(data)
        if count != length or check_hash.hexdigest() != source_hash.hexdigest():
            sys.exit("BLOCKED: compressed chunk does not match verified image")
        parts.append({
            "part": index,
            "offset": offset,
            "raw_bytes": length,
            "raw_sha256": source_hash.hexdigest(),
            "gzip_name": gzip_path.name,
            "gzip_bytes": gzip_path.stat().st_size,
            "gzip_sha256": sha256_file(gzip_path),
        })
        print(f"PASS G567_PART={index} raw={length} gzip={gzip_path.stat().st_size}", flush=True)
if sum(x["raw_bytes"] for x in parts) != EXPECTED_IMAGE_SIZE:
    sys.exit("BLOCKED: fragment lengths do not cover SYSTEM")
manifest = {
    "device": "SM-T311",
    "targetfiles_sha256": EXPECTED_ZIP_SHA256,
    "raw_system_sha256": EXPECTED_IMAGE_SHA256,
    "raw_system_bytes": EXPECTED_IMAGE_SIZE,
    "chunk_size": PART_SIZE,
    "parts": parts,
}
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"G567_CHUNKS_SHA256_VERIFIED={len(parts)}", flush=True)
