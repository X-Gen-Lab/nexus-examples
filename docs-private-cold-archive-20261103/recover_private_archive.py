#!/usr/bin/env python3
"""Authenticate and verify this exact private opaque stream; optional explicit file recovery."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from urllib.request import Request,urlopen
REPOSITORY='X-Gen-Lab/nexus-examples'
NAMESPACE='docs-private-cold-archive-20261103'
EXPECTED_BYTES=2783689352
EXPECTED_SHA='f9d2ca85464384e52a424d8fc355b138d7eb9c6339cc120079b32f55c8f7cdf4'
LIMIT=32*1024*1024
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--commit',required=True)
parser.add_argument('--output',type=Path)
a=parser.parse_args()
if not re.fullmatch(r'[0-9a-f]{40}',a.commit):raise ValueError('Exact immutable commit required')
token=os.environ.get('GH_TOKEN')
if not token:
 r=subprocess.run(['gh','auth','token'],capture_output=True,text=True,timeout=30)
 if r.returncode or not r.stdout.strip():raise ValueError('Configured authentication unavailable')
 token=r.stdout.strip()
def chunks(path,maximum):
 if not path.startswith(NAMESPACE+'/') or '/' in path[len(NAMESPACE)+1:] or '..' in path:raise ValueError('Exact same-level namespace member required')
 req=Request('https://api.github.com/repos/'+REPOSITORY+'/contents/'+path+'?ref='+a.commit,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github.raw+json','X-GitHub-Api-Version':'2022-11-28'})
 count=0
 try:
  with urlopen(req,timeout=120) as response:
   while data:=response.read(1024*1024):
    count+=len(data)
    if count>maximum:raise ValueError('Remote member exceeded bound')
    yield data
 except Exception:
  raise ValueError('Authenticated exact-commit member read failed') from None
manifest=json.loads(b''.join(chunks(NAMESPACE+'/transport.json',LIMIT)))
plan=manifest['opaque_stream']
if plan['bytes']!=EXPECTED_BYTES or plan['sha256']!=EXPECTED_SHA or manifest['archive_original_identity']['mode']!=0o600:raise ValueError('Unexpected fixed archive identity')
if len(plan['parts'])!=83 or len(plan['segments'])!=2 or [len(s['parts']) for s in plan['segments']]!=[63,20]:raise ValueError('Unexpected fixed segment closure')
output=None
if a.output:
 target=a.output.absolute()
 if target!=target.resolve() or any(p.is_symlink() for p in (target,*target.parents)) or not target.parent.is_dir() or target.exists():raise ValueError('New canonical explicit output required')
 partial=target.with_name(target.name+'.partial')
 if partial.exists() or shutil.disk_usage(target.parent).free<EXPECTED_BYTES+64*1024*1024:raise ValueError('No complete recovery capacity or unused temporary path')
 output=partial.open('xb')
whole,total=hashlib.sha256(),0
segment_hashes=[hashlib.sha256(),hashlib.sha256()];segment_sizes=[0,0];seen=set()
try:
 for index,p in enumerate(plan['parts']):
  if p['whole_index']!=index or p['offset']!=total or p['path'] in seen or not 0<p['bytes']<=LIMIT or p['segment_index'] not in (0,1):raise ValueError('Invalid contiguous part closure')
  seen.add(p['path']);h,count=hashlib.sha256(),0
  for data in chunks(p['path'],p['bytes']):
   count+=len(data);h.update(data);whole.update(data);total+=len(data)
   segment_hashes[p['segment_index']].update(data);segment_sizes[p['segment_index']]+=len(data)
   if output:output.write(data)
  if count!=p['bytes'] or h.hexdigest()!=p['sha256']:raise ValueError('Remote part identity differs')
 for index,s in enumerate(plan['segments']):
  selected=[p for p in plan['parts'] if p['segment_index']==index]
  if (s['index']!=index or [p['path'] for p in selected]!=s['parts'] or [p['segment_part'] for p in selected]!=list(range(len(selected))) or s['offset']!=selected[0]['offset'] or segment_sizes[index]!=s['bytes'] or segment_hashes[index].hexdigest()!=s['sha256']):raise ValueError('Remote segment identity differs')
 if total!=EXPECTED_BYTES or whole.hexdigest()!=EXPECTED_SHA:raise ValueError('Complete remote archive identity differs')
 for m in manifest['metadata']:
  h,count=hashlib.sha256(),0
  for data in chunks(m['path'],m['bytes']):count+=len(data);h.update(data)
  if count!=m['bytes'] or h.hexdigest()!=m['sha256']:raise ValueError('Remote original metadata/helper identity differs')
 if output:
  output.flush();os.fsync(output.fileno());output.close();output=None
  partial.chmod(0o600);partial.replace(target)
 print(json.dumps({'status':'complete_authenticated_opaque_stream_verified','bytes':total,'sha256':whole.hexdigest(),'output':str(target) if a.output else None,'fresh_inner_tree_restore':False,'new_source_qualification':False}))
finally:
 if output:output.close()
 # On failure keep the uniquely named partial as an explicitly incomplete artifact.
