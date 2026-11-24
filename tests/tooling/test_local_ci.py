"""Local CI rejects incomplete execution and preserves real failure evidence."""

import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

import yaml

import local_ci
from sdk_tools import ROOT, sdk_root


class LocalCiContracts(unittest.TestCase):
    def test_output_never_reuses_old_results_or_traverses_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "new result"
            local_ci.prepare_output(output)
            with self.assertRaisesRegex(ValueError, "exist"):
                local_ci.prepare_output(output)
            (root / "link").symlink_to(output, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlink"):
                local_ci.prepare_output(root / "link/run")

    def test_missing_tool_is_a_failure_before_any_build(self):
        with self.assertRaisesRegex(ValueError, "missing.*nexus-no-such-tool"):
            local_ci.require_tools(["nexus-no-such-tool"], os.environ)

    def test_real_failed_process_retains_exit_command_and_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            runner = local_ci.LocalCI(ROOT, sdk_root(), output, 1)
            with self.assertRaisesRegex(ValueError, "23"):
                runner.command("failure", [sys.executable, "-c",
                                           "print('actual failure'); raise SystemExit(23)"])
            receipt = json.loads((output / "failure.command.json").read_text())
            self.assertEqual(receipt["exit_code"], 23)
            self.assertIn("actual failure", (output / "failure.log").read_text())

    def test_installed_ctest_uses_sdk_validator_for_empty_skipped_and_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.xml"
            for contents in ("<testsuite/>",
                             '<testsuite><testcase name="x"><skipped/></testcase></testsuite>'):
                report.write_text(contents)
                with self.subTest(contents=contents), self.assertRaises(ValueError):
                    local_ci.validate_junit(sdk_root(), report, 0)
            report.write_text('<testsuite><testcase name="x"/></testsuite>')
            with self.assertRaises(ValueError):
                local_ci.validate_junit(sdk_root(), report, time.time_ns() + 10**9)
            self.assertEqual(local_ci.validate_junit(sdk_root(), report, 0)["passed"], 1)

    def test_arm_selection_matches_the_existing_presets_and_hosted_matrix(self):
        workflow = yaml.safe_load((ROOT / ".github/workflows/ci.yml").read_text())
        matrix = workflow["jobs"]["arm"]["strategy"]["matrix"]["preset"]
        self.assertEqual(matrix, "${{ fromJSON(needs.native.outputs.arm_presets) }}")
        self.assertEqual(len(local_ci.arm_presets(ROOT)), 8)
        with self.assertRaisesRegex(ValueError, "preset"):
            local_ci.arm_presets(ROOT, ["../../unknown"])

    def test_fixtures_are_explicit_and_unknown_boards_cannot_prepare_hil(self):
        self.assertEqual(local_ci.fixture_for("stm32-qiming-baremetal"),
                         "stm32f407_qiming_v31")
        with self.assertRaises(ValueError):
            local_ci.fixture_for("gd32-f303-reference-freertos")
        with self.assertRaisesRegex(ValueError, "fixture"):
            local_ci.fixture_for("unreviewed-board-baremetal")

    def test_installed_consumer_has_no_source_fallback_and_exact_expected_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            runner = local_ci.LocalCI(ROOT, sdk_root(), output, 1)
            runner.identity = {"nexus_revision": "a" * 40, "nexus_tree": "b" * 40}
            commands = []
            runner.command = lambda name, argv: commands.append((name, list(map(str, argv))))
            with mock.patch.object(local_ci, "verify_sdk"), \
                    mock.patch.object(local_ci, "validate_junit", return_value={"passed": 1}):
                runner.installed(output / "SDK with spaces")
            configure = next(argv for name, argv in commands if name == "installed-configure")
            self.assertIn("-DNEXUS_EXPECTED_SOURCE_REVISION=" + "a" * 40, configure)
            self.assertIn("-DNEXUS_TEST_SUPPORT=ON", configure)
            self.assertNotIn("-DNEXUS_BUILD_TESTS=OFF", configure)
            self.assertNotIn("-DNEXUS_BUILD_WORKLOAD=OFF", configure)
            self.assertNotIn("NEXUS_SOURCE", runner.environment)
            self.assertEqual(runner.environment.get("PYTHONDONTWRITEBYTECODE"), "1")
            for name, argv in commands:
                if name == "installed-build":
                    self.assertEqual(argv[-2:], ["--parallel", "1"])
            self.assertFalse(any(arg.startswith("-DNEXUS_SOURCE=") for arg in configure))
            for recipe in ("differential_drive", "servo_axis", "lift_axis"):
                name = "installed-" + recipe
                configure = next(argv for step, argv in commands
                                 if step == name + "-configure")
                self.assertIn(str(ROOT / "recipes/control" / recipe), configure)
                self.assertIn("-DNexus_DIR=" + str(output / "SDK with spaces/lib/cmake/Nexus"),
                              configure)
                self.assertIn("-DNEXUS_ENABLE_SANITIZERS=ON", configure)
                self.assertIn(str(output / name), configure)
                self.assertFalse(any(arg.startswith("-DNEXUS_SOURCE=") for arg in configure))
                build = next(argv for step, argv in commands if step == name + "-build")
                self.assertIn(recipe + "_reference", build)
                ctest = next(argv for step, argv in commands if step == name + "-ctest")
                self.assertIn("--no-tests=error", ctest)
                self.assertIn(str(output / name / "ctest-results.xml"), ctest)
            self.assertEqual(set(runner.report["installed_references"]),
                             {"differential_drive", "servo_axis", "lift_axis"})
            self.assertEqual(commands[-1][0], "installed-sdk-after")

    def test_installed_payload_rejection_after_ctest_cannot_qualify_the_run(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            runner = local_ci.LocalCI(ROOT, sdk_root(), output, 1)
            runner.identity = {"nexus_revision": "a" * 40, "nexus_tree": "b" * 40}
            calls = []

            def command(name, argv):
                calls.append(name)
                if name == "installed-sdk-after":
                    raise ValueError("SDK payload changed during installed execution")

            runner.command = command
            with mock.patch.object(local_ci, "verify_sdk", return_value={}), \
                    mock.patch.object(local_ci, "validate_junit", return_value={"passed": 1}), \
                    self.assertRaisesRegex(ValueError, "payload changed"):
                runner.installed(output / "SDK")
            self.assertLess(calls.index("installed-ctest"), calls.index("installed-sdk-after"))

    def test_reused_sdk_verifier_receives_its_manifest_root_not_install_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "relocated SDK"
            runner = local_ci.LocalCI(Path(directory), sdk_root(), Path(directory), 1)
            calls = []
            runner.command = lambda name, argv, *args: calls.append((name, list(map(str, argv))))
            runner.installed = lambda installed: self.assertEqual(installed, prefix)
            with mock.patch.object(local_ci, "verify_sdk"), \
                    mock.patch.object(runner, "retain_native"):
                runner.native(prefix)
            verify = next(argv for name, argv in calls if name == "sdk-verify")
            self.assertEqual(verify[verify.index("--verify") + 1],
                             str(prefix / "share/nexus/src"))

    def test_formal_packaging_explicitly_includes_the_drive_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            runner = local_ci.LocalCI(Path(directory), sdk_root(), Path(directory), 1)
            calls = []
            runner.command = lambda name, argv, *args: calls.append((name, list(map(str, argv))))
            runner.installed = lambda prefix: None
            with mock.patch.object(runner, "retain_native"):
                runner.native(None)
            package = next(argv for name, argv in calls if name == "sdk-package")
            selected = [package[index + 1] for index, value in enumerate(package)
                        if value == "--component"]
            self.assertEqual(selected, ["network", "tls", "mqtt", "cia402"])

    def test_wrong_reused_sdk_fails_before_expensive_native_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "SDK"
            manifest = prefix / "share/nexus/src/.nexus-source-sdk.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"publishable": false}')
            runner = local_ci.LocalCI(ROOT, sdk_root(), Path(directory), 1)
            runner.identity = {"nexus_revision": "a" * 40, "nexus_tree": "b" * 40}
            calls = []
            runner.command = lambda name, argv, *args: calls.append(name)
            with self.assertRaises(ValueError):
                runner.native(prefix)
            self.assertEqual(calls, [])

    def test_sdk_identity_cannot_substitute_another_commit_or_development_package(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            manifest = prefix / "share/nexus/src/.nexus-source-sdk.json"
            manifest.parent.mkdir(parents=True)
            identity = {"nexus_revision": "a" * 40, "nexus_tree": "b" * 40}
            for document in ({}, [], None, {"publishable": False},
                             {"publishable": True, "source": {"commit": "c" * 40}}):
                manifest.write_text(json.dumps(document))
                with self.assertRaises(ValueError):
                    local_ci.verify_sdk(prefix, identity)
            manifest.write_text(json.dumps({"publishable": True, "source_dirty": False,
                                           "source_revision": "a" * 40,
                                           "source_tree": "b" * 40}))
            self.assertTrue(local_ci.verify_sdk(prefix, identity)["publishable"])

    def test_warm_cache_cannot_select_a_different_arm_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, toolchain = root / "build", root / "locked-toolchain"
            build.mkdir()
            cache = build / "CMakeCache.txt"
            cache.write_text("CMAKE_C_COMPILER:FILEPATH=/unreviewed/arm-none-eabi-gcc\n")
            with self.assertRaisesRegex(ValueError, "compiler"):
                local_ci.verify_arm_cache(build, toolchain)
            cache.write_text(f"CMAKE_C_COMPILER:STRING={toolchain}/bin/arm-none-eabi-gcc\n")
            local_ci.verify_arm_cache(build, toolchain)

    def test_current_standalones_match_f407_software_recipes(self):
        rows = [row for preset in local_ci.arm_presets(ROOT)
                for row in local_ci.standalones(preset)]
        self.assertEqual({
            ("apps/shared_spi_runtime", "shared_spi_runtime", None),
            ("apps/usb_configuration", "usb_configuration", None),
            (".", "platform_reference", "apps/platform_reference/dma.toml"),
            ("apps/shared_spi_runtime", "finite_spi_dma",
             "apps/shared_spi_runtime/dma.toml"),
            ("apps/stream_recovery", "adc_window",
             "apps/stream_recovery/adc-window.toml"),
            ("apps/stream_recovery", "audio_window",
             "apps/stream_recovery/audio-window.toml"),
            ("recipes/control/motor_drive", "motor_drive_reference", None),
            *(("templates/" + name, "nexus_starter", None)
              for name in ("qiming", "sky", "qiming-core", "qiming-v41")),
        }, set(rows))


    def test_explicit_sdk_commands_do_not_require_the_default_checkout(self):
        import sdk_tools

        source = sdk_root()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "run"
            output.mkdir()
            with mock.patch.dict(os.environ, dict(os.environ), clear=True), \
                    mock.patch.object(sdk_tools, "ROOT", root):
                os.environ.pop("NEXUS_SOURCE", None)
                runner = local_ci.LocalCI(root, source, output, 1)
                runner.command("override", [sys.executable, "-c",
                                            "print('explicit SDK executed')"])
            receipt = json.loads((output / "override.command.json").read_text())
            self.assertEqual(receipt["exit_code"], 0)
            self.assertIn("explicit SDK executed", (output / "override.log").read_text())

    def test_native_result_belongs_to_this_run_after_another_lease_owner(self):
        from concurrent.futures import ThreadPoolExecutor
        from sdk_tools import maintenance

        source = sdk_root()
        lease, _ = maintenance(source)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "run"
            output.mkdir()
            build = root / "build/native-debug"
            shared = build / "tdd/execution.json"
            shared.parent.mkdir(parents=True)
            runner = local_ci.LocalCI(root, source, output, 1)
            own = {"kind": "example-development-contract-execution",
                   "marker": "this run"}
            other = {"kind": "example-development-contract-execution",
                     "marker": "next run"}

            def command(name, arguments, environment=None):
                if name == "native":
                    with lease.build_lock(build):
                        shared.write_text(json.dumps(own))

            def next_owner():
                try:
                    with lease.build_lock(build):
                        shared.write_text(json.dumps(other))
                except ValueError:
                    return "busy"
                return "replaced"

            def installed(prefix):
                with ThreadPoolExecutor(max_workers=1) as pool:
                    pool.submit(next_owner).result(timeout=5)

            runner.command = command
            runner.installed = installed
            with mock.patch.object(local_ci, "verify_sdk"):
                runner.native(root / "installed SDK")
            retained = Path(runner.report["native_gate"])
            self.assertEqual(json.loads(retained.read_text()), own)
            self.assertTrue(retained.is_relative_to(output))
            # A later legitimate owner may replace the common latest view.
            with lease.build_lock(build):
                shared.write_text(json.dumps(other))
            self.assertEqual(json.loads(retained.read_text()), own)


    def test_qiming_arm_runs_usb_and_starter_with_unchanged_other_recipes(self):
        expected = {
            "stm32-qiming-freertos": [("apps/shared_spi_runtime", "shared_spi_runtime", None)],
            "stm32-sky-baremetal": [("templates/sky", "nexus_starter", None)],
            "stm32-qiming-core-baremetal": [("templates/qiming-core", "nexus_starter", None)],
            "stm32-qiming-v41-baremetal": [("templates/qiming-v41", "nexus_starter", None)],
        }
        for preset in local_ci.arm_presets(ROOT):
            if preset != "stm32-qiming-baremetal":
                self.assertEqual(local_ci.standalones(preset), expected.get(preset, []))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "run"
            output.mkdir()
            runner = local_ci.LocalCI(root, sdk_root(), output, 1)
            commands = []
            runner.command = lambda name, argv: commands.append((name, list(map(str, argv))))
            runner.arm_preset("stm32-qiming-baremetal", root / "build/arm", root / "toolchain")
            configure = [argv for name, argv in commands if name.endswith("-configure")]
            for arguments in configure:
                self.assertIn("-DNexus_DIR=" + str(sdk_root() / "cmake"), arguments)
                self.assertFalse(any(argument.startswith("-DNEXUS_SOURCE=")
                                     for argument in arguments))
            self.assertEqual({str(root / "templates/qiming"),
                              str(root / "apps/usb_configuration"), str(root),
                              str(root / "apps/shared_spi_runtime"),
                              str(root / "apps/stream_recovery"),
                              str(root / "recipes/control/motor_drive")},
                             {argv[argv.index("-S") + 1] for argv in configure})
            for target, assembly in (
                    ("platform_reference", "apps/platform_reference/dma.toml"),
                    ("finite_spi_dma", "apps/shared_spi_runtime/dma.toml"),
                    ("adc_window", "apps/stream_recovery/adc-window.toml"),
                    ("audio_window", "apps/stream_recovery/audio-window.toml")):
                command = next(argv for name, argv in commands if name ==
                               "stm32-qiming-baremetal-" + target + "-configure")
                self.assertIn("-DNEXUS_ASSEMBLY_FILE=" + str(root / assembly),
                              command)
            build = next(argv for name, argv in commands
                         if name == "stm32-qiming-baremetal-usb_configuration-build")
            self.assertIn("usb_configuration", build)
            resources = next(argv for name, argv in commands
                             if name == "stm32-qiming-baremetal-usb_configuration-resources")
            self.assertEqual(resources[resources.index("--elf") + 1],
                             str(output / "stm32-qiming-baremetal-usb_configuration/bin/usb_configuration.elf"))
            resources = next(argv for name, argv in commands
                             if name == "stm32-qiming-baremetal-motor_drive_reference-resources")
            self.assertEqual(resources[resources.index("--elf") + 1],
                             str(output / "stm32-qiming-baremetal-motor_drive_reference/bin/motor_drive_reference.elf"))


if __name__ == "__main__":
    unittest.main()
