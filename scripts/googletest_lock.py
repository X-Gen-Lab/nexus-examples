#!/usr/bin/env python3
"""Admit the project's locked host framework independently of the platform SDK."""

import configparser
import glob
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = 'external/googletest'
ORIGIN = 'https://github.com/X-Gen-Lab/googletest.git'
UPSTREAM = '6910c9d9165801d8827d628cb72eb7ea9dd538c5'


def regular(path):
    path = Path(path).absolute()
    if not path.is_file() or any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError('Framework metadata must be a regular file: ' + str(path))
    return path


def git(root, *arguments, parent_index=False):
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith('GIT_')}
    environment['GIT_OPTIONAL_LOCKS'] = '0'
    if parent_index and 'GIT_INDEX_FILE' in os.environ:
        environment['GIT_INDEX_FILE'] = os.environ['GIT_INDEX_FILE']
    result = subprocess.run(['git', '-C', str(root), *arguments],
        env=environment, capture_output=True, text=True, timeout=20)
    if result.returncode:
        raise ValueError('Cannot inspect framework Git ' + arguments[0])
    return result.stdout.strip()


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate framework lock field: ' + key)
        result[key] = value
    return result


def verify(root=ROOT):
    root = Path(root).absolute()
    source = root / PATH
    if (not source.is_dir() or not (source / '.git').exists() or
            any(p.is_symlink() for p in (source, *source.parents))):
        raise ValueError('Initialize the regular project GoogleTest submodule')
    lock_path = regular(root / 'dependencies/googletest.lock.json')
    lock = json.loads(lock_path.read_text(), object_pairs_hook=unique)
    expected = {'schema', 'name', 'version', 'commit', 'upstream_commit', 'path',
                'url', 'upstream_url', 'license', 'scope'}
    if (not isinstance(lock, dict) or set(lock) != expected or
            type(lock['schema']) is not int or lock['schema'] != 1 or
            lock['name'] != 'googletest' or lock['version'] != '1.16.0' or
            lock['upstream_commit'] != UPSTREAM or lock['path'] != PATH or
            lock['url'] != ORIGIN or lock['license'] != 'BSD-3-Clause' or
            lock['upstream_url'] != 'https://github.com/google/googletest.git' or
            lock['scope'] != 'host tests only; no firmware or configure-time network' or
            not isinstance(lock['commit'], str) or
            not re.fullmatch('[0-9a-f]{40}', lock['commit'])):
        raise ValueError('Exact reviewed project GoogleTest lock required')
    modules = regular(root / '.gitmodules')
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(modules.read_text())
    section = parser['submodule "' + PATH + '"']
    if parser.defaults() or dict(section) != {'path': PATH, 'url': ORIGIN}:
        raise ValueError('Framework origin declaration differs from lock')
    if Path(git(source, 'rev-parse', '--show-toplevel')) != source:
        raise ValueError('Framework requires its own Git checkout')
    record = git(root, 'ls-files', '--stage', '--', PATH, parent_index=True)
    if record != '160000 ' + lock['commit'] + ' 0\t' + PATH:
        raise ValueError('Framework parent gitlink differs from lock')
    if git(source, 'rev-parse', 'HEAD') != lock['commit']:
        raise ValueError('Framework checkout commit differs from lock')
    if git(source, 'rev-parse', '--symbolic-full-name', 'HEAD') != 'HEAD':
        raise ValueError('Framework submodule must be detached at its locked commit')
    git(source, 'merge-base', '--is-ancestor', UPSTREAM, lock['commit'])
    if git(source, 'config', '--get-all', 'remote.origin.url') != ORIGIN:
        raise ValueError('Framework origin differs from lock')
    if git(source, 'status', '--porcelain', '--untracked-files=all'):
        raise ValueError('Framework checkout is dirty')
    cmake = regular(source / 'CMakeLists.txt')
    if re.findall(r'^set\(GOOGLETEST_VERSION ([0-9.]+)\)$',
                  cmake.read_text(), re.MULTILINE) != [lock['version']]:
        raise ValueError('Framework source version differs from lock')
    inputs = [lock_path, modules, Path(__file__), cmake, regular(source / 'LICENSE')]
    directory = Path(git(source, 'rev-parse', '--absolute-git-dir'))
    candidates = [source / '.git', root / '.git', directory / 'commondir']
    for name in ('HEAD', 'index', 'config', 'config.worktree'):
        path = Path(git(source, 'rev-parse', '--git-path', name))
        candidates.append(path if path.is_absolute() else source / path)
    parent_index = Path(git(root, 'rev-parse', '--git-path', 'index', parent_index=True))
    inputs.append(parent_index if parent_index.is_absolute() else root / parent_index)
    inputs += [path for path in candidates if path.is_file()]
    return {'commit': lock['commit'], 'version': lock['version'],
            'configure_inputs': [str(regular(path)) for path in inputs],
            'configure_file_probes': [glob.escape(path.absolute().as_posix())
                                      for path in candidates]}


if __name__ == '__main__':
    try:
        print(json.dumps(verify(), sort_keys=True))
    except (OSError, ValueError, KeyError, configparser.Error) as error:
        print('Project GoogleTest rejected: ' + str(error), file=sys.stderr)
        raise SystemExit(1) from error
