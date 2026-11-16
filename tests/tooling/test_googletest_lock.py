"""Check project-owned host framework admission using real small Git fixtures."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import googletest_lock


ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = '6910c9d9165801d8827d628cb72eb7ea9dd538c5'
ORIGIN = 'https://github.com/X-Gen-Lab/googletest.git'


class ProjectGoogleTestLockTests(unittest.TestCase):
    def git(self, root, *args):
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith('GIT_')}
        command = ['git', '-c', 'user.name=Framework fixture',
                   '-c', 'user.email=fixture@example.invalid', '-C', str(root), *args]
        result = subprocess.run(command, env=environment, capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='example GTest [fixture] ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'external/googletest'
        self.source.mkdir(parents=True)
        self.git(self.root, 'init', '--quiet')
        self.git(self.source, 'init', '--quiet')
        # Only the real upstream commit object is needed for this tiny source
        # fixture. Do not serve an entire promisor checkout through upload-pack.
        upstream = subprocess.check_output(['git', '-C',
            str(ROOT / 'external/googletest'), 'cat-file', 'commit', UPSTREAM])
        written = subprocess.run(['git', '-C', str(self.source), 'hash-object',
            '-w', '-t', 'commit', '--stdin'], input=upstream,
            capture_output=True, check=True).stdout.decode().strip()
        self.assertEqual(written, UPSTREAM)
        (self.source / '.git/shallow').write_text(UPSTREAM + '\n')
        (self.source / 'CMakeLists.txt').write_text('set(GOOGLETEST_VERSION 1.16.0)\n')
        (self.source / 'LICENSE').write_text('Fixture BSD notice\n')
        self.git(self.source, 'add', '.')
        tree = self.git(self.source, 'write-tree')
        commit = self.git(self.source, 'commit-tree', tree, '-p', UPSTREAM,
                          '-m', 'framework fixture')
        self.git(self.source, 'update-ref', 'HEAD', commit)
        self.git(self.source, 'checkout', '--quiet', '--detach')
        self.git(self.source, 'remote', 'add', 'origin', ORIGIN)
        self.lock = json.loads((ROOT / 'dependencies/googletest.lock.json').read_text())
        self.lock['commit'] = self.git(self.source, 'rev-parse', 'HEAD')
        self.lock_path = self.root / 'dependencies/googletest.lock.json'
        self.lock_path.parent.mkdir()
        self.write_lock()
        (self.root / '.gitmodules').write_text(
            '[submodule "external/googletest"]\n'
            '\tpath = external/googletest\n\turl = ' + ORIGIN + '\n')
        self.git(self.root, 'update-index', '--add', '--cacheinfo', '160000',
                 self.lock['commit'], 'external/googletest')

    def write_lock(self):
        self.lock_path.write_text(json.dumps(self.lock) + '\n')

    def test_exact_clean_framework_and_regular_metadata_are_admitted(self):
        result = googletest_lock.verify(self.root)
        self.assertEqual(result['commit'], self.lock['commit'])
        self.assertTrue(result['configure_inputs'])
        self.assertTrue(all(Path(path).is_file() for path in result['configure_inputs']))

    def test_dirty_source_wrong_origin_and_wrong_gitlink_are_rejected(self):
        notice = self.source / 'LICENSE'
        notice.write_text('Unreviewed source edit\n')
        with self.assertRaisesRegex(ValueError, 'dirty'):
            googletest_lock.verify(self.root)
        self.git(self.source, 'checkout', '--', 'LICENSE')
        self.git(self.source, 'remote', 'set-url', 'origin', 'https://invalid.test/src')
        with self.assertRaisesRegex(ValueError, 'origin'):
            googletest_lock.verify(self.root)
        self.git(self.source, 'remote', 'set-url', 'origin', ORIGIN)
        self.git(self.root, 'update-index', '--cacheinfo', '160000',
                 'a' * 40, 'external/googletest')
        with self.assertRaisesRegex(ValueError, 'gitlink'):
            googletest_lock.verify(self.root)

    def test_parent_alternate_index_never_redirects_framework_observation(self):
        alternate = self.root / 'proposed-index'
        self.git(self.root, 'read-tree', '--empty')
        with patch.dict(os.environ, {'GIT_INDEX_FILE': str(alternate)}):
            result = subprocess.run(['git', '-C', str(self.root), 'update-index',
                '--add', '--cacheinfo', '160000', self.lock['commit'],
                'external/googletest'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(googletest_lock.verify(self.root)['commit'], self.lock['commit'])
        with self.assertRaisesRegex(ValueError, 'gitlink'):
            googletest_lock.verify(self.root)

    def test_unknown_lock_field_and_symlink_source_are_rejected(self):
        self.lock['fallback'] = True
        self.write_lock()
        with self.assertRaisesRegex(ValueError, 'lock'):
            googletest_lock.verify(self.root)
        del self.lock['fallback']
        self.write_lock()
        moved = self.source.with_name('moved-source')
        self.source.rename(moved)
        self.source.symlink_to(moved, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'regular'):
            googletest_lock.verify(self.root)


if __name__ == '__main__':
    unittest.main()
