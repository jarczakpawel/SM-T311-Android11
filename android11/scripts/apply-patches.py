#!/usr/bin/env python3
"""Apply 34 final SM-T311 binary Git diffs atop pinned upstream AOSP source.

Usage: python3 android11/scripts/apply-patches.py /path/to/lineage-18.1
Prerequisite: 'repo sync' using android11/manifests/base.xml.
Each reconstructed source tree must match the originally built Git tree SHA.
"""
import hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
INDEX=ROOT/'android11/patches/index.json'
def git(repo,*args):
 p=subprocess.run(['git','-C',str(repo),*args],capture_output=True,text=True)
 if p.returncode:raise SystemExit('FAIL '+str(repo)+' '+repr(args[:4])+': '+p.stderr[-500:])
 return p.stdout.strip()
def sha(f):
 h=hashlib.sha256()
 with f.open('rb') as s:
  for block in iter(lambda:s.read(1<<20),b''):h.update(block)
 return h.hexdigest()
def main():
 if len(sys.argv)!=2:raise SystemExit('usage: apply-patches.py /path/to/android11')
 workspace=Path(sys.argv[1]).expanduser().resolve()
 if not (workspace/'.repo').is_dir():raise SystemExit('Missing .repo: first run repo init/sync with manifests/base.xml')
 rows=json.loads(INDEX.read_text())
 if len(rows)!=34:raise SystemExit('Expected 34 source patches')
 for i,row in enumerate(rows):
  repo=workspace/row['repo'];patch=ROOT/row['patch']
  if not repo.is_dir() or sha(patch)!=row['patch_sha256']:
   raise SystemExit('Missing repo or patch checksum mismatch: '+row['repo'])
  actual_head=git(repo,'rev-parse','HEAD')
  if actual_head==row['final_commit'] and git(repo,'status','--porcelain','--untracked-files=no')=='':
   print('already accepted',row['repo'])
   continue
  if actual_head!=row['base_commit']:
   raise SystemExit('Unexpected base HEAD for '+row['repo']+': '+actual_head)
  if git(repo,'status','--porcelain','--untracked-files=no'):
   raise SystemExit('Tracked source modifications; refuse overwrite: '+row['repo'])
  # --index means the state is equally visible to Git and normal AOSP tools.
  git(repo,'apply','--index','--binary',str(patch))
  tree=git(repo,'write-tree')
  if tree!=row['final_tree']:
   raise SystemExit('Result does not match accepted G1786 tree: '+row['repo'])
  # A synthetic publication checkout commit intentionally differs in commit ID
  # from the original; bundled Git histories can restore exact historical ID.
  git(repo,'-c','user.name=SM-T311 Source Release','-c',
      'user.email=release@invalid','commit','-m','SM-T311 G1786 verified source snapshot from upstream base')
  final_tree=git(repo,'rev-parse','HEAD^{tree}')
  if final_tree!=row['final_tree']:raise SystemExit('Post-commit tree mismatch '+row['repo'])
  print(f'[{i+1}/34] OK',row['repo'])
 print('PASS: all 34 project source trees exactly match the built G1786 revision')
if __name__=='__main__':main()
