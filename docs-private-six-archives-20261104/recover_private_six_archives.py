#!/usr/bin/env python3
"""Exact private six-archive byte recovery; no inner restore or qualification."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from urllib.request import Request,urlopen
REPOSITORY='X-Gen-Lab/nexus-examples'
NAMESPACE='docs-private-six-archives-20261104'
LIMIT=32*1024**2
FIXED=[{'id': 'old-native-debug', 'bytes': 785709349, 'sha256': '1aeccae46c04a82cf1b21dc2bfd402680647b541c5a8072e0a2315ffc0d56283', 'mode': 384, 'parts': 24}, {'id': 'ordinary-native-producer', 'bytes': 654022083, 'sha256': 'eccab58d563e1e01e60e34452152b22d7748feba2ab0d875a2fdd265dceae3bf', 'mode': 384, 'parts': 20}, {'id': 'native-intermediate', 'bytes': 270199403, 'sha256': 'e1a8000cc781c0936fc49900d4a3d30d3b9bd6961fed8eddb736c496ed7eb836', 'mode': 384, 'parts': 9}, {'id': 'candidate-ce616ea9', 'bytes': 434248932, 'sha256': 'ef270bca90ebe3ca64211430efe1440a887de36e9f8d3d78c74640835d53c12a', 'mode': 384, 'parts': 13}, {'id': 'candidate-fc01d863', 'bytes': 434092822, 'sha256': 'a443e9c3b39528d5d5fbbdd19ee1c065496cd86b8df78cb5380aab4505bf3aa9', 'mode': 384, 'parts': 13}, {'id': 'candidate-92e66923', 'bytes': 433850915, 'sha256': '7f89e8ec33a543e150e7165a3e4252aa11a96d62fe5d3187a9b0d4791aa618b4', 'mode': 384, 'parts': 13}]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--commit',required=True)
parser.add_argument('--archive-id',choices=[x['id'] for x in FIXED])
parser.add_argument('--output',type=Path)
a=parser.parse_args()
if not re.fullmatch('[0-9a-f]{40}',a.commit) or (a.output and not a.archive_id):raise ValueError('Exact commit and one explicit output archive ID required')
token=os.environ.get('GH_TOKEN')
if not token:
 r=subprocess.run(['gh','auth','token'],capture_output=True,text=True,timeout=30)
 if r.returncode or not r.stdout.strip():raise ValueError('Configured authentication unavailable')
 token=r.stdout.strip()
def chunks(path,bound):
 if not path.startswith(NAMESPACE+'/') or '/' in path[len(NAMESPACE)+1:] or '..' in path:raise ValueError('Exact same-level namespace member required')
 req=Request('https://api.github.com/repos/'+REPOSITORY+'/contents/'+path+'?ref='+a.commit,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github.raw+json','X-GitHub-Api-Version':'2022-11-28'})
 n=0
 try:
  with urlopen(req,timeout=120) as response:
   while block:=response.read(1024*1024):
    n+=len(block)
    if n>bound:raise ValueError('Remote response exceeded bound')
    yield block
 except Exception:raise ValueError('Authenticated immutable member recovery failed') from None
manifest=json.loads(b''.join(chunks(NAMESPACE+'/transport.json',LIMIT)))
if manifest['repository']!=REPOSITORY or manifest['namespace']!=NAMESPACE or len(manifest['archives'])!=6:raise ValueError('Wrong fixed private transport')
results=[]
for expected,item in zip(FIXED,manifest['archives']):
 if any(item['original'][k]!=expected[k] for k in ('id','bytes','sha256','mode')):raise ValueError('Wrong fixed original archive')
 if a.archive_id and expected['id']!=a.archive_id:continue
 plan=item['opaque_stream'];seen=set();total=0;whole=hashlib.sha256();output=None
 if plan['bytes']!=expected['bytes'] or plan['sha256']!=expected['sha256'] or len(plan['parts'])!=expected['parts'] or len(plan['segments'])!=1:raise ValueError('Wrong fixed opaque extent')
 if a.output:
  target=a.output.absolute();partial=target.with_name(target.name+'.partial')
  if target!=target.resolve() or any(p.is_symlink() for p in (target,*target.parents)) or not target.parent.is_dir() or target.exists() or partial.exists():raise ValueError('New canonical explicit output required')
  if shutil.disk_usage(target.parent).free<expected['bytes']+64*1024**2:raise ValueError('Complete archive output capacity unavailable')
  output=partial.open('xb')
 try:
  for index,p in enumerate(plan['parts']):
   if p['whole_index']!=index or p['segment_index']!=0 or p['segment_part']!=index or p['offset']!=total or p['path']!=NAMESPACE+'/'+expected['id']+'.part'+str(index).zfill(4) or p['path'] in seen or not 0<p['bytes']<=LIMIT or (index<len(plan['parts'])-1 and p['bytes']!=LIMIT):raise ValueError('Part sequence differs')
   seen.add(p['path']);h,n=hashlib.sha256(),0
   for block in chunks(p['path'],p['bytes']):
    n+=len(block);h.update(block);whole.update(block);total+=len(block)
    if output:output.write(block)
   if n!=p['bytes'] or h.hexdigest()!=p['sha256']:raise ValueError('Part bytes differ')
  s=plan['segments'][0]
  if total!=expected['bytes'] or whole.hexdigest()!=expected['sha256'] or s!={'index':0,'offset':0,'bytes':total,'sha256':whole.hexdigest(),'parts':[p['path'] for p in plan['parts']]}:raise ValueError('Complete original archive differs')
  if output:output.flush();os.fsync(output.fileno());output.close();output=None;partial.chmod(expected['mode']);partial.replace(target)
  results.append({'id':expected['id'],'bytes':total,'sha256':whole.hexdigest(),'output':str(target) if a.output else None})
 finally:
  if output:output.close()
for m in manifest['metadata']:
 h,n=hashlib.sha256(),0
 for block in chunks(m['path'],m['bytes']):n+=len(block);h.update(block)
 if n!=m['bytes'] or h.hexdigest()!=m['sha256']:raise ValueError('Original metadata/helper differs')
print(json.dumps({'status':'complete_authenticated_selected_opaque_streams_verified','archives':results,'fresh_inner_tree_restore':False,'new_source_qualification':False}))
