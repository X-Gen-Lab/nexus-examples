"""Check candidate isolation with real Git identities and recursive Gitlinks.

The temporary SDK/import helper is a tooling fixture, not an official SDK build.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("repository_identity", HERE / "test_repository.py")
repository = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repository)


def git(directory, *arguments):
    result = subprocess.run(["git", "-c", "user.name=SDK fixture",
                             "-c", "user.email=fixture@example.invalid", *arguments],
                            cwd=directory, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.strip()


class CandidateSDKIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="candidate SDK fixture ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "examples"
        self.root.mkdir()
        git(self.root, "init", "--quiet")
        self.source = self.root / "external/nexus"
        self.source.mkdir(parents=True)
        git(self.source, "init", "--quiet")
        git(self.source, "remote", "add", "origin", "https://github.com/X-Gen-Lab/nexus.git")
        (self.source / "CMakeLists.txt").write_text("# identity fixture only\n")
        vendor = self.source / "scripts/ci/verify_vendor_source.py"
        vendor.parent.mkdir(parents=True)
        vendor.write_text('print(\'{"kind":"tooling-import-fixture"}\')\n')
        gitmodules = []
        # Model the new source SDK closure independently of its verifier.
        dependencies = [name for name in repository.DEPENDENCIES
                        if not name.startswith("vendors/arm/")]
        dependencies.append("vendors/arm/CMSIS_6")
        for name in dependencies:
            dependency = self.source / name
            dependency.mkdir(parents=True)
            git(dependency, "init", "--quiet")
            (dependency / "fixture.txt").write_text("identity only\n")
            git(dependency, "add", "fixture.txt")
            git(dependency, "commit", "--quiet", "-m", "fixture dependency")
            revision = git(dependency, "rev-parse", "HEAD")
            git(self.source, "update-index", "--add", "--cacheinfo", f"160000,{revision},{name}")
            gitmodules.append(f'[submodule "{name}"]\n\tpath = {name}\n\turl = https://example.invalid/{name}.git\n')
        (self.source / ".gitmodules").write_text("".join(gitmodules))
        git(self.source, "add", "CMakeLists.txt", ".gitmodules", "scripts")
        git(self.source, "commit", "--quiet", "-m", "formal SDK fixture")
        git(self.source, "submodule", "init")
        self.pinned = git(self.source, "rev-parse", "HEAD")
        self.pinned_tree = git(self.source, "rev-parse", "HEAD^{tree}")
        (self.root / "dependencies").mkdir()
        (self.root / "dependencies/nexus.lock.json").write_text(json.dumps({
            "schema_version": 1,
            "repository": "https://github.com/X-Gen-Lab/nexus.git",
            "commit": self.pinned, "tree": self.pinned_tree,
            "consumption": "source-add_subdirectory", "recursive_submodules": True,
        }))
        shutil.copyfile(HERE.parent / "CMakePresets.json", self.root / "CMakePresets.json")
        (self.root / ".gitmodules").write_text('[submodule "external/nexus"]\n\tpath = external/nexus\n\turl = https://github.com/X-Gen-Lab/nexus.git\n')
        git(self.root, "update-index", "--add", "--cacheinfo", f"160000,{self.pinned},external/nexus")
        git(self.root, "add", ".gitmodules", "dependencies", "CMakePresets.json")
        git(self.root, "commit", "--quiet", "-m", "formal examples fixture")
        (self.source / "candidate.txt").write_text("candidate identity only\n")
        git(self.source, "add", "candidate.txt")
        git(self.source, "commit", "--quiet", "-m", "candidate SDK fixture")
        self.candidate = git(self.source, "rev-parse", "HEAD")
        self.previous_root = repository.ROOT
        repository.ROOT = self.root
        self.addCleanup(setattr, repository, "ROOT", self.previous_root)

    def test_candidate_checks_real_source_without_changing_formal_pin(self):
        lock_before = (self.root / "dependencies/nexus.lock.json").read_bytes()
        gitlink_before = git(self.root, "ls-files", "--stage", "--", "external/nexus")
        report = repository.verify(self.source, False, self.candidate)
        self.assertTrue(report["candidate_validation"])
        self.assertEqual(report["candidate_nexus_revision"], self.candidate)
        self.assertEqual(report["formal_locked_nexus_revision"], self.pinned)
        self.assertEqual(report["nexus_tree"], git(self.source, "rev-parse", "HEAD^{tree}"))
        self.assertFalse(report["source_revision_matches_lock"])
        self.assertEqual(len(report["submodules"]), len(repository.DEPENDENCIES))
        self.assertIn("vendors/arm/CMSIS_6",
                      {entry["path"] for entry in report["submodules"]})
        self.assertEqual(lock_before, (self.root / "dependencies/nexus.lock.json").read_bytes())
        self.assertEqual(gitlink_before, git(self.root, "ls-files", "--stage", "--", "external/nexus"))
        self.assertFalse(report["hardware_verified"])

    def test_normal_verification_still_rejects_unpinned_candidate(self):
        with self.assertRaisesRegex(ValueError, "revision mismatch"):
            repository.verify(self.source, False)
        git(self.source, "checkout", "--quiet", "--detach", self.pinned)
        report = repository.verify(self.source, False)
        self.assertFalse(report["candidate_validation"])
        self.assertTrue(report["source_revision_matches_lock"])

    def test_candidate_cannot_accept_a_different_checked_out_commit(self):
        with self.assertRaisesRegex(ValueError, "revision mismatch"):
            repository.verify(self.source, False, self.pinned)

    def test_candidate_rejects_dirty_dependency_and_wrong_repository(self):
        dependency = self.source / repository.DEPENDENCIES[0] / "fixture.txt"
        dependency.write_text("changed\n")
        with self.assertRaisesRegex(ValueError, "uncommitted changes"):
            repository.verify(self.source, False, self.candidate)
        dependency.write_text("identity only\n")
        git(self.source, "remote", "set-url", "origin", "https://example.invalid/other.git")
        with self.assertRaisesRegex(ValueError, "origin does not match"):
            repository.verify(self.source, False, self.candidate)

    def test_candidate_rejects_missing_central_cmsis_core(self):
        shutil.rmtree(self.source / "vendors/arm/CMSIS_6")
        with self.assertRaisesRegex(ValueError, "uncommitted changes"):
            repository.verify(self.source, False, self.candidate)

    def test_candidate_rejects_branch_short_sha_or_argument_text(self):
        for value in ("main", self.candidate[:12], "", "--help", self.candidate.upper()):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                repository.candidate_commit(value)

    def test_nested_directory_cannot_inherit_ancestor_git_identity(self):
        nested = self.root / "uninitialized-source"
        nested.mkdir()
        (nested / "CMakeLists.txt").write_text("# unrelated directory\n")
        with self.assertRaisesRegex(ValueError, "own complete Git checkout"):
            repository.verify(nested, True, self.candidate)


if __name__ == "__main__":
    unittest.main()
