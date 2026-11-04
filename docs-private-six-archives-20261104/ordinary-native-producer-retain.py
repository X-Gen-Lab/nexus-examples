import json,shutil,sys,time
from pathlib import Path
sys.path.insert(0,'/workspace/nexus')
from tools.evidence.store import export_tree,restore_tree,inventory
source=Path('/dev/shm/nexus-docs-recovery-20261103/validation/build/native-debug/bin')
out=Path('/workspace/nexus/build/recovery-20261103/tls-ipo/ordinary/native-producer-bytes')
restored=Path('/tmp/nexus-native-producer-bytes-restored-20261103')
def record(phase,**values):(out/'state.json').write_text(json.dumps({'phase':phase,'updated_ns':time.time_ns(),**values},indent=2)+'\n')
try:
 assert not restored.exists()
 before=inventory(source);record('archive',files=sum(r['kind']=='file' for r in before))
 package=export_tree(source,out/'archive');(out/'package.json').write_text(json.dumps(package,indent=2)+'\n')
 record('actually_restoring',archive=package['archive'])
 restore_tree(package,restored)
 after=inventory(restored);assert after==before and inventory(source)==before
 record('completed',files=package['files'],bytes=package['bytes'],archive=package['archive'],actual_restore_verified=True,scope='All422actualNativeproducerbinaries preserved; ordinarysource/hash/context receipts remain separatelycanonical. Othergeneratedobjects are rebuildable cache, notretainedpayload. No newexecutionqualification.')
 shutil.rmtree(restored)
 print((out/'state.json').read_text(),flush=True)
except Exception as error:record('failed',error=repr(error));raise
