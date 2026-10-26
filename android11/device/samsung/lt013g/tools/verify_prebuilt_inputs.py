#!/usr/bin/env python3
"""Check all required local non-Git build inputs without copying or uploading blobs.

Every input is verified byte-for-byte against the locally recorded known-good
SHA256.  This checker makes no representation about redistribution rights.
"""
import argparse
import hashlib
import json
from pathlib import Path


def hash_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def check(root, manifest):
    dataset = json.loads(manifest.read_text())
    results = []
    for record in dataset['entries']:
        relative = Path(record['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('unsafe manifest path: ' + str(relative))
        file = root / relative
        actual_size = file.stat().st_size if file.is_file() else None
        actual_hash = hash_file(file) if actual_size == record['size_bytes'] else None
        passed = actual_hash == record['sha256']
        results.append({'path':str(relative),'status':'PASS' if passed else 'MISSING_OR_MISMATCH','expected_sha256':record['sha256'],'actual_sha256':actual_hash,'actual_bytes':actual_size,'expected_bytes':record['size_bytes'],'group':record['provenance_group']})
    return {'status':'PASS' if all(r['status']=='PASS' for r in results) else 'FAIL','checked':len(results),'results':results,'public_redistribution_granted':False}


if __name__ == '__main__':
    a=argparse.ArgumentParser()
    a.add_argument('--aosp-root',type=Path,required=True)
    a.add_argument('--manifest',type=Path,default=Path(__file__).with_name('g265-binary-build-input-manifest.json'))
    a.add_argument('--json-out',type=Path)
    args=a.parse_args()
    result=check(args.aosp_root,args.manifest)
    print(json.dumps({'status':result['status'],'checked':result['checked'],'failed':[r['path'] for r in result['results'] if r['status']!='PASS']},indent=2))
    if args.json_out:
        args.json_out.parent.mkdir(parents=True,exist_ok=True)
        args.json_out.write_text(json.dumps(result,indent=2)+'\n')
    raise SystemExit(result['status']!='PASS')
