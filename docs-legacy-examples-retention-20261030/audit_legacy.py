"""Read immutable legacy archives in place; emit private inventory only."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile

ROOT = Path('/workspace/nexus')
sys.path.insert(0, str(ROOT))
from tools.evidence import store

os.umask(0o077)
OUT = ROOT / 'build/current-completion-20261030/publication/private-audit'
OUT.mkdir(mode=0o700, exist_ok=False)
WORK = ROOT / 'build/c-cold-20261028/p6-external-retention/worktree'
OLD = ROOT / 'build/c-cold-20261028/private-legacy-example-routing-20261029/legacy-example-routing-inventory.json'
EXAMPLES = Path('/workspace/nexus-examples')

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

inventory = json.loads(OLD.read_text())
for member in inventory['members']:
    path = WORK / member['path']
    if not path.is_file() or path.is_symlink():
        raise ValueError('missing original immutable member')
    if path.stat().st_size != member['bytes'] or sha(path) != member['sha256']:
        raise ValueError('original immutable member identity differs')

# Compare whole file bytes with tracked files in existing local history only.
# No remote fetch, source text publication, or inference from filenames.
revisions = subprocess.check_output(['git', '-C', str(EXAMPLES), 'rev-list',
                                    '--all', '--max-count=200'], text=True).split()
private_blobs = {}
for revision in revisions:
    records = subprocess.check_output(['git', '-C', str(EXAMPLES), 'ls-tree',
                                      '-r', '-z', revision])
    for record in records.split(b'\0'):
        if not record:
            continue
        identity, name = record.split(b'\t', 1)
        mode, kind, blob = identity.split()
        if kind != b'blob' or mode not in {b'100644', b'100755'}:
            continue
        path = os.fsdecode(name)
        if path.startswith('external/'):
            continue
        private_blobs.setdefault(blob.decode(), set()).add(path)

archives = []
for member in inventory['members']:
    if member['classification'] == 'archive':
        archives.append((WORK / member['path'], member['sha256'], None))
for ns, envelope in [('docs-platform-delivery-20261029-g5d60b56e',
                       'examples-build-package.json'),
                      ('docs-platform-delivery-20261029-gd742cdb5',
                       'examples.tar.gz.package.json')]:
    package = json.loads((WORK / ns / envelope).read_text())
    path = Path(package['archive']['path'])
    parts = sorted((WORK / ns).glob('*examples*.tar.gz.part*'))
    combined, size = hashlib.sha256(), 0
    for part in parts:
        with part.open('rb') as f:
            while block := f.read(1024 * 1024):
                combined.update(block)
                size += len(block)
    if combined.hexdigest() != package['archive']['sha256'] or size != package['archive']['size']:
        raise ValueError('published multipart bytes differ from original package')
    archives.append((path, package['archive']['sha256'], package))

summary = []
for index, (path, checksum, package) in enumerate(archives):
    if sha(path) != checksum:
        raise ValueError('original archive identity mismatch')
    verification = (store.verify_package(package) if package else
                    store._external_metrics(path))
    members, matches, count, total = [], [], 0, 0
    with tarfile.open(path, 'r|gz') as archive:
        seen = set()
        for entry in archive:
            name = store._safe_path(entry.name.rstrip('/'))
            count += 1
            total += entry.size
            if name in seen or count > 200000 or total > 4 * 1024**3:
                raise ValueError('duplicate or excessive legacy archive')
            seen.add(name)
            if not (entry.isfile() or entry.isdir()):
                raise ValueError('nonregular legacy archive entry')
            record = {'path': name, 'size': entry.size,
                      'kind': 'file' if entry.isfile() else 'directory'}
            if entry.isfile():
                content = archive.extractfile(entry)
                h = hashlib.sha256()
                blob = hashlib.sha1(f'blob {entry.size}\0'.encode())
                while block := content.read(1024 * 1024):
                    h.update(block)
                    blob.update(block)
                record['sha256'] = h.hexdigest()
                matched = private_blobs.get(blob.hexdigest())
                if matched:
                    matches.append({'archive_member': name,
                                    'sha256': record['sha256'],
                                    'tracked_private_repository_paths': sorted(matched)})
            members.append(record)
            archive.members.clear()
    destination = OUT / f'archive-{index:02d}.json'
    data = {'kind': 'private_legacy_archive_contents',
            'classification': 'private/internal; do not publish',
            'archive': {'path': str(path), 'sha256': checksum,
                        'bytes': path.stat().st_size},
            'maintained_archive_verification': verification,
            'entries': members, 'tracked_private_repository_byte_matches': matches,
            'limitations': ['Byte matches identify tracked repository files, not proprietary status.',
                           'Absence of a match does not prove absence of private content.',
                           'Compiled code, embedded strings and credentials are not classified by this inventory.']}
    destination.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
    summary.append({'archive_sha256': checksum, 'members': count,
                    'expanded_bytes': total, 'exact_tracked_file_matches': len(matches),
                    'inventory_path': str(destination),
                    'inventory_sha256': sha(destination)})
    print(json.dumps(summary[-1]), flush=True)
result = {'kind': 'private_legacy_archive_inspection',
          'classification': 'private/internal; do not publish',
          'original_inventory_sha256': sha(OLD),
          'local_private_repository_revisions_compared': len(revisions),
          'tracked_blob_identities_compared': len(private_blobs),
          'archives': summary,
          'actions': {'network_fetch': False, 'archive_extraction': False,
                      'source_modification': False, 'history_rewrite': False,
                      'published_withdrawal': False}}
(OUT / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
