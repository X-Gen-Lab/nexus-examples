"""The adapter preserves real child failures and never invents an execution."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

from apps.network_gateway import broker_client


class GatewayBrokerClient(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="gateway bundle ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.calls = self.root / "calls.jsonl"
        self.failure = self.root / "failure.txt"
        self.credentials = self.root / "credentials"
        self.credentials.mkdir()
        self.client = self.root / "gateway"
        self.client.write_text(
            f"#!{sys.executable}\n"
            "import json,pathlib,sys\n"
            f"calls=pathlib.Path({str(self.calls)!r})\n"
            f"failure=pathlib.Path({str(self.failure)!r})\n"
            "with calls.open('a') as stream:\n"
            " stream.write(json.dumps(sys.argv[1:])+'\\n')\n"
            "print('actual_child_mode='+sys.argv[2],flush=True)\n"
            "raise SystemExit(9 if failure.exists() and "
            "failure.read_text()==sys.argv[2] else 0)\n")
        self.client.chmod(0o700)

    def records(self):
        return [json.loads(line) for line in self.calls.read_text().splitlines()]

    def test_clean_bundle_runs_three_actual_processes_and_retains_their_receipts(self):
        output = self.root / "result"
        self.assertEqual(broker_client.execute_bundle(
            self.client, "23456", "clean", self.credentials, output), 0)
        records = self.records()
        self.assertEqual([row[1] for row in records],
                         ["clean", "backpressure", "deadline"])
        self.assertTrue(all(row[0] == "23456" and row[2] == str(self.credentials)
                            for row in records))
        receipts = list(output.glob("*.command.json"))
        self.assertEqual(len(receipts), 3)
        self.assertTrue(all(json.loads(path.read_text())["exit_code"] == 0
                            for path in receipts))

    def test_middle_failure_is_returned_and_prevents_the_later_execution(self):
        self.failure.write_text("backpressure")
        output = self.root / "failure"
        self.assertEqual(broker_client.execute_bundle(
            self.client, "23456", "clean", self.credentials, output), 9)
        self.assertEqual([row[1] for row in self.records()],
                         ["clean", "backpressure"])
        receipt = json.loads((output / "backpressure.command.json").read_text())
        self.assertEqual(receipt["exit_code"], 9)

    def test_unknown_bundle_rejects_before_creating_output_or_a_child(self):
        output = self.root / "invalid"
        with self.assertRaises(ValueError):
            broker_client.execute_bundle(self.client, "23456", "../unknown",
                                         self.credentials, output)
        self.assertFalse(output.exists())
        self.assertFalse(self.calls.exists())
