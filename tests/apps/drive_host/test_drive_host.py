"""Real bounded host application with an injected external CAN port."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "drive_host", ROOT / "recipes/network/drive_host/host.py"
)
host = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = host
SPEC.loader.exec_module(host)


class Clock:
    def __init__(self):
        self.now = 10.0

    def __call__(self):
        return self.now


class Port:
    def __init__(self, clock):
        self.clock = clock
        self.sent = []
        self.pending = []
        self.state = 0x40
        self.velocity = 0
        self.mode = 3
        self.wrong_mode = False
        self.delay = 0.001
        self.fail_motion_once = False
        self.stop_unconfirmed = False
        self.inject_error = False
        self.stop_gap = False
        self.stop_reports = 0

    def send(self, frame, deadline):
        if self.clock() >= deadline:
            raise TimeoutError("send expired")
        self.sent.append(frame)
        if frame.can_id == 0:
            return
        control, mode, raw = struct.unpack("<Hbi", frame.data)
        if raw and self.fail_motion_once:
            self.fail_motion_once = False
            raise OSError("injected bus unavailable")
        self.mode = 1 if self.wrong_mode else mode
        self.state = {6: 0x21, 7: 0x23, 15: 0x27, 11: 0x07}[control]
        self.velocity = raw
        if control == 11 and self.stop_unconfirmed:
            self.state = 0x27
            self.velocity = 100
        self.pending.append(self.clock() + self.delay)

    def receive(self, deadline):
        if self.inject_error and self.state == 0x27:
            self.inject_error = False
            raise OSError("injected receive bus error")
        arrival = self.pending.pop(0) if self.pending else self.clock() + 0.005
        if self.state == 0x07:
            self.stop_reports += 1
            if self.stop_gap and self.stop_reports > 1:
                arrival = self.clock() + 0.04
        if arrival >= deadline:
            self.clock.now = deadline
            return None
        self.clock.now = arrival
        return host.Frame(0x192,
                          struct.pack("<Hbi", self.state | 0x210,
                                      self.mode, self.velocity), arrival)


def config():
    return host.Config(node_id=18, velocity_unit_rad_s=0.01,
                       maximum_velocity_raw=1000, period_s=0.01,
                       feedback_timeout_s=0.03, stop_timeout_s=0.05,
                       stop_dwell_s=0.01, stop_velocity_rad_s=0.05)


class Codec(unittest.TestCase):
    def test_real_pv_mapping_and_negative_units_match_c_profile(self):
        self.assertEqual(host.encode_pv(15, 3, 2.5, 0.01, 1000),
                         bytes.fromhex("0f 00 03 fa 00 00 00"))
        frame = host.Frame(0x192, bytes.fromhex("37 02 03 83 ff ff ff"), 10)
        feedback = host.decode_feedback(frame, 18, 0.01)
        self.assertEqual(feedback.state, 0x27)
        self.assertAlmostEqual(feedback.velocity_rad_s, -1.25)
        self.assertEqual(feedback.mode, 3)

    def test_rounding_is_away_from_zero_and_invalid_inputs_reject(self):
        self.assertEqual(struct.unpack("<Hbi", host.encode_pv(
            15, 3, -0.015, 0.01, 1000))[2], -2)
        for velocity, unit, limit in ((float("nan"), .01, 1000),
                                      (1, 0, 1000), (20, .01, 1000),
                                      (1, .01, True)):
            with self.subTest(values=(velocity, unit, limit)):
                with self.assertRaises(ValueError):
                    host.encode_pv(15, 3, velocity, unit, limit)

    def test_wrong_frame_shape_and_error_flags_never_become_feedback(self):
        for frame in (host.Frame(0x192, b"short", 10),
                      host.Frame(0x80000192, b"\0" * 7, 10),
                      host.Frame(0x20000192, b"\0" * 7, 10)):
            with self.assertRaises(ValueError):
                host.decode_feedback(frame, 18, .01)


class Sequence(unittest.TestCase):
    def test_finite_velocity_requires_actual_state_then_requests_quick_stop(self):
        clock = Clock()
        port = Port(clock)
        request = host.VelocityRequest(-1.25, 10, 10.08)
        report = host.run_velocity(port, request, config(), monotonic=clock)
        self.assertTrue(report.enabled_confirmed)
        self.assertGreater(report.velocity_frames_sent, 0)
        self.assertTrue(report.quick_stop_sent)
        self.assertTrue(report.quick_stop_state_reported)
        self.assertTrue(report.zero_velocity_dwell_reported)
        self.assertFalse(report.physical_stop_verified)
        self.assertEqual(report.reference_adoption, "not_reported_by_profile")
        self.assertIsNone(report.error)
        self.assertEqual(report.requested_velocity_rad_s, -1.25)
        self.assertEqual(report.wire_velocity_rad_s, -1.25)
        controls = [struct.unpack("<Hbi", x.data)[0]
                    for x in port.sent if x.can_id != 0]
        self.assertEqual(controls[:3], [6, 7, 15])
        self.assertEqual(controls[-1], 11)
        self.assertLess(clock(), request.valid_until + config().stop_timeout_s)

    def test_mode_not_effective_never_sends_nonzero_motion(self):
        clock = Clock()
        port = Port(clock)
        port.wrong_mode = True
        report = host.run_velocity(port, host.VelocityRequest(2, 10, 10.08),
                                   config(), monotonic=clock)
        self.assertFalse(report.enabled_confirmed)
        self.assertEqual(report.velocity_frames_sent, 0)
        self.assertIsNotNone(report.error)
        self.assertTrue(all(struct.unpack("<Hbi", f.data)[2] == 0
                            for f in port.sent if f.can_id != 0))

    def test_expired_or_future_authority_rejects_before_can_effects(self):
        for issued, expires in ((9, 10), (11, 12), (10, float("inf"))):
            clock = Clock()
            port = Port(clock)
            with self.assertRaises(ValueError):
                host.run_velocity(port, host.VelocityRequest(1, issued, expires),
                                  config(), monotonic=clock)
            self.assertEqual(port.sent, [])

    def test_delayed_handshake_cannot_extend_original_authority(self):
        clock = Clock()
        port = Port(clock)
        port.delay = 1
        request = host.VelocityRequest(2, 10, 10.02)
        report = host.run_velocity(port, request, config(), monotonic=clock)
        self.assertFalse(report.enabled_confirmed)
        self.assertEqual(report.velocity_frames_sent, 0)
        self.assertLessEqual(clock(), request.valid_until +
                             config().stop_timeout_s)

    def test_bus_failure_preserves_error_and_attempts_finite_cleanup(self):
        clock = Clock()
        port = Port(clock)
        port.fail_motion_once = True
        report = host.run_velocity(port, host.VelocityRequest(2, 10, 10.08),
                                   config(), monotonic=clock)
        self.assertIn("bus unavailable", report.error)
        self.assertTrue(report.quick_stop_sent)
        self.assertTrue(report.quick_stop_state_reported)
        self.assertFalse(report.physical_stop_verified)

    def test_receive_error_never_counts_as_motion_or_stop_confirmation(self):
        clock = Clock()
        port = Port(clock)
        port.inject_error = True
        report = host.run_velocity(port, host.VelocityRequest(2, 10, 10.08),
                                   config(), monotonic=clock)
        self.assertIn("bus error", report.error)
        self.assertEqual(report.velocity_frames_sent, 0)

    def test_linux_error_frame_is_not_skipped_as_unrelated_traffic(self):
        clock = Clock()
        port = Port(clock)
        original = port.receive
        injected = False

        def error_frame(deadline):
            nonlocal injected
            if port.state == 0x27 and not injected:
                injected = True
                return host.Frame(host.ERROR_FLAG | 0x40, b"\0" * 8, clock())
            return original(deadline)

        port.receive = error_frame
        report = host.run_velocity(port, host.VelocityRequest(2, 10, 10.08),
                                   config(), monotonic=clock)
        self.assertIn("controller error frame", report.error)
        self.assertEqual(report.velocity_frames_sent, 0)
        self.assertTrue(report.quick_stop_sent)

    def test_stop_without_reported_state_and_velocity_remains_unconfirmed(self):
        clock = Clock()
        port = Port(clock)
        port.stop_unconfirmed = True
        report = host.run_velocity(port, host.VelocityRequest(1, 10, 10.05),
                                   config(), monotonic=clock)
        self.assertTrue(report.quick_stop_sent)
        self.assertFalse(report.quick_stop_state_reported)
        self.assertFalse(report.zero_velocity_dwell_reported)
        self.assertIsNotNone(report.stop_error)
        self.assertFalse(report.physical_stop_verified)

    def test_sparse_stop_reports_cannot_be_used_as_continuous_dwell(self):
        clock = Clock()
        port = Port(clock)
        port.stop_gap = True
        report = host.run_velocity(port, host.VelocityRequest(1, 10, 10.05),
                                   config(), monotonic=clock)
        self.assertTrue(report.quick_stop_state_reported)
        self.assertFalse(report.zero_velocity_dwell_reported)
        self.assertIsNotNone(report.stop_error)

    def test_feedback_observed_before_request_never_confirms_its_state(self):
        clock = Clock()
        port = Port(clock)
        original = port.receive

        def old_feedback(deadline):
            frame = original(deadline)
            return None if frame is None else host.Frame(
                frame.can_id, frame.data, 9)

        port.receive = old_feedback
        report = host.run_velocity(port, host.VelocityRequest(1, 10, 10.05),
                                   config(), monotonic=clock)
        self.assertFalse(report.enabled_confirmed)
        self.assertEqual(report.velocity_frames_sent, 0)


class Cli(unittest.TestCase):
    def test_default_only_prints_codec_demo_without_opening_can(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(host.main([]), 0)
        data = json.loads(output.getvalue())
        self.assertEqual(data["scope"], "codec_demo_no_hardware")
        self.assertEqual(data["rpdo_hex"], "0f0003fa000000")

    def test_partial_hardware_arguments_reject_before_port_open(self):
        with mock.patch.object(host, "SocketCAN") as port:
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    host.main(["--interface", "can0", "--node", "18"])
            self.assertEqual(error.exception.code, 2)
            port.assert_not_called()


class SocketPort(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.socket = mock.Mock()
        self.factory = mock.patch.object(host.socket, "socket", return_value=self.socket)
        self.factory.start()
        self.addCleanup(self.factory.stop)

    def test_actual_classic_frame_and_selected_tpdo_filters(self):
        port = host.SocketCAN("can0", 18, monotonic=self.clock)
        self.socket.bind.assert_called_once_with(("can0",))
        self.socket.setsockopt.assert_any_call(
            host.socket.SOL_CAN_RAW, host.socket.CAN_RAW_FILTER,
            struct.pack("=II", 0x192, 0xC00007FF))
        self.socket.send.return_value = 16
        frame = host.Frame(0x212, bytes.fromhex("0f 00 03 fa 00 00 00"), 10)
        with mock.patch.object(host.select, "select", return_value=([], [self.socket], [])):
            port.send(frame, 10.1)
        self.socket.send.assert_called_once_with(
            struct.pack("=IB3x8s", 0x212, 7, frame.data + b"\0"))
        self.socket.recv.return_value = struct.pack(
            "=IB3x8s", 0x192, 7, bytes.fromhex("37 02 03 83 ff ff ff 00"))
        with mock.patch.object(host.select, "select", return_value=([self.socket], [], [])):
            received = port.receive(10.1)
        self.assertAlmostEqual(host.decode_feedback(received, 18, .01).velocity_rad_s,
                               -1.25)
        port.close()
        self.socket.close.assert_called_once()

    def test_select_late_return_cannot_send_expired_authority(self):
        port = host.SocketCAN("can0", 18, monotonic=self.clock)
        self.socket.send.return_value = 16

        def late_select(*_):
            self.clock.now = 10.2
            return [], [self.socket], []

        with mock.patch.object(host.select, "select", side_effect=late_select):
            with self.assertRaises(TimeoutError):
                port.send(host.Frame(0x212, b"\0" * 7, 10), 10.1)
        self.socket.send.assert_not_called()

    def test_failed_bind_closes_socket(self):
        self.socket.bind.side_effect = OSError("interface unavailable")
        with self.assertRaises(OSError):
            host.SocketCAN("can0", 18, monotonic=self.clock)
        self.socket.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
