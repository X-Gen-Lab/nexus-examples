import hashlib,json,os,shutil,stat,subprocess,sys,time
from pathlib import Path
root=Path('/workspace/nexus');sys.path.insert(0,str(root))
from tools.dev import process
control=root/'build/recovery-20261103/cold-cache-retention'
source=root/'build/c-cold-20261028'
temp=Path('/tmp/nexus-cold-cache-retention-20261103');restored=temp/'restored'
archive=temp/'c-cold-20261028.tar.zst'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def record(phase,**kw):
 (control/'state.json').write_text(json.dumps({'phase':phase,'updated_ns':time.time_ns(),**kw},indent=2)+'\n')
try:
 assert source.is_dir() and not source.is_symlink() and not temp.exists()
 worktrees=subprocess.check_output(['git','worktree','list','--porcelain'],cwd=root,text=True)
 assert not any(line.startswith('worktree '+str(source)) for line in worktrees.splitlines())
 temp.mkdir();before=shutil.disk_usage(root).free
 record('inventory')
 members=[]
 for directory, dirs, files in os.walk(source,followlinks=False):
  for name in sorted(dirs+files):
   p=Path(directory)/name;s=p.lstat();rel=p.relative_to(root).as_posix()
   if stat.S_ISLNK(s.st_mode):members.append({'path':rel,'type':'symlink','target':os.readlink(p)})
   elif stat.S_ISREG(s.st_mode):members.append({'path':rel,'type':'file','size':s.st_size,'sha256':sha(p)})
   elif not stat.S_ISDIR(s.st_mode):raise ValueError('Unexpected special cache member '+rel)
 inventory=temp/'members.json';inventory.write_text(json.dumps({'members':members},indent=2)+'\n')
 env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}
 record('archive',members=len(members),bytes=sum(m.get('size',0) for m in members))
 r=process.run(['tar','--use-compress-program=zstd -T1 -1','-cf',str(archive),'build/c-cold-20261028'],cwd=root,env=env,log=control/'archive.log',stage='archive-ended-historical-cold-cache',timeout=1800)
 if r.returncode:raise RuntimeError('archive failed')
 record('actually_restoring',archive_sha256=sha(archive));restored.mkdir()
 r=process.run(['tar','--use-compress-program=zstd -d -T1','-xf',str(archive),'-C',str(restored)],cwd=root,env=env,log=control/'restore.log',stage='restore-ended-historical-cold-cache',timeout=1800)
 if r.returncode:raise RuntimeError('restore failed')
 for member in members:
  p=restored/member['path']
  if member['type']=='symlink':assert p.is_symlink() and os.readlink(p)==member['target']
  else:assert p.is_file() and not p.is_symlink() and p.stat().st_size==member['size'] and sha(p)==member['sha256']
 record('restored_bytes_verified',members=len(members),archive_sha256=sha(archive))
 for member in members:
  p=root/member['path']
  if member['type']=='symlink':assert p.is_symlink() and os.readlink(p)==member['target']
  else:assert p.is_file() and not p.is_symlink() and p.stat().st_size==member['size'] and sha(p)==member['sha256']
 retired=root/'build/c-cold-20261028.retired-20261103';assert not retired.exists()
 source.rename(retired);shutil.rmtree(retired)
 after=shutil.disk_usage(root).free
 final=control/'c-cold-20261028.tar.zst'
 if after<archive.stat().st_size+64*1024*1024:raise RuntimeError('Deletion did not free enough upper-layer space to persist archive; archive remains in '+str(archive))
 shutil.move(str(archive),str(final));shutil.move(str(inventory),str(control/'members.json'))
 receipt={'archive':str(final),'archive_sha256':sha(final),'archive_bytes':final.stat().st_size,'verified_regular_members':sum(m['type']=='file' for m in members),'verified_symlink_identity_members':sum(m['type']=='symlink' for m in members),'historical_alias_targets':'metadata retained only; temporary target bytes did not survive restart, no functional object-cache restoration claimed','free_before':before,'free_after_deletion':after,'free_after_persisting_archive':shutil.disk_usage(root).free,'scope':'ended historical c-cold-20261028 build only; current source/Git/private trees/other caches untouched','qualification':'actual retention check only; not new test execution'}
 (control/'retention.json').write_text(json.dumps(receipt,indent=2)+'\n')
 shutil.rmtree(restored)
 record('completed',**receipt)
 print(json.dumps(receipt),flush=True)
except Exception as e:
 record('failed',error=repr(e));raise
