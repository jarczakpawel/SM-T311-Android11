#!/usr/bin/env python3
"""Restore exact original local commit SHAs from separately uploaded release Git bundles.

Original history archive has 34 incremental bundles with verified upstream
prerequisites, plus two Samsung device full-history bundles and Linux full
history. Does not require pushing to or forking 863 AOSP repositories.
"""
import hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
INDEX=ROOT/'android11/patches/index.json'
def run(*args):
 p=subprocess.run(args,capture_output=True,text=True)
 if p.returncode:raise SystemExit('FAILED: '+' '.join(args[:7])+' '+p.stderr[-500:])
 return p.stdout.strip()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 if len(sys.argv)!=3:raise SystemExit('usage: restore-original-git-history.py /path/to/android11 /path/to/extracted/source-history')
 workspace=Path(sys.argv[1]).expanduser().resolve()
 hist=Path(sys.argv[2]).expanduser().resolve()
 if not (workspace/'.repo').is_dir():raise SystemExit('Expected a repo-synced Android source tree')
 if not (hist/'SHA256SUMS.txt').is_file():raise SystemExit('Missing source-history/SHA256SUMS.txt')
 rows=json.loads(INDEX.read_text())
 sums=dict()
 for line in (hist/'SHA256SUMS.txt').read_text().splitlines():
  digest,name=line.split(maxsplit=1)
  sums[name.strip()]=digest
 for row in rows:
  d=workspace/row['repo']
  name=row['repo'].replace('/','__')+'.bundle'
  bundle=hist/name
  if not bundle.is_file() or sha(bundle)!=sums.get(name):
   raise SystemExit('Git bundle missing or sha mismatch: '+name)
  now=run('git','-C',str(d),'rev-parse','HEAD')
  if now==row['final_commit']:
   print('already at exact original commit',row['repo'])
   continue
  if now!=row['base_commit'] or run('git','-C',str(d),'status','--porcelain','--untracked-files=no'):
   raise SystemExit('Refuse to change unknown working tree: '+row['repo'])
  run('git','-C',str(d),'fetch',str(bundle),'HEAD')
  if run('git','-C',str(d),'rev-parse','FETCH_HEAD')!=row['final_commit']:
   raise SystemExit('Wrong original commit in bundle '+row['repo'])
  run('git','-C',str(d),'checkout','-B','sm-t311-G1786',row['final_commit'])
  tree=run('git','-C',str(d),'rev-parse','HEAD^{tree}')
  if tree!=row['final_tree']:
   raise SystemExit('Git history content mismatch '+row['repo'])
  print('original SHA verified',row['repo'],row['final_commit'][:12])
 print('OK: original 34-branch Android Git history reconstructed without code changes')
if __name__=='__main__':main()
