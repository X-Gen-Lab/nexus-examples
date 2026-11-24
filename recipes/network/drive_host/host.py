#!/usr/bin/env python3
"""Finite host-side use of Nexus's selected asynchronous CiA 402 PV profile.

No hardware is opened on import or in the default codec demonstration. This is
an application, not a drive, controller, command-lease server or ROS 2 runtime.
"""

import argparse
from dataclasses import asdict, dataclass
import json
import math
import select
import socket
import struct
import time


PV = struct.Struct("<Hbi")
CAN_FRAME = struct.Struct("=IB3x8s")
STATE_MASK = 0x006F
ERROR_FLAG = 0x20000000
REMOTE_FLAG = 0x40000000
EXTENDED_FLAG = 0x80000000
# Linux UAPI linux/can/raw.h; CPython does not expose this option on every build.
CAN_RAW_ERR_FILTER = 2


def _finite(value):
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value))


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def encode_pv(controlword, mode, velocity_rad_s, unit_rad_s, maximum_raw):
    """Encode this profile's seven bytes; scaling is explicit product data."""
    if (not _integer(controlword, 0, 65535) or
            not _integer(mode, -128, 127) or not _finite(velocity_rad_s) or
            not _finite(unit_rad_s) or unit_rad_s <= 0 or
            not _integer(maximum_raw, 1, 2147483647)):
        raise ValueError("invalid PV control, scaling or velocity")
    value = velocity_rad_s / unit_rad_s
    if not math.isfinite(value):
        raise ValueError("velocity scaling overflow")
    raw = math.floor(abs(value) + 0.5)
    if value < 0:
        raw = -raw
    if abs(raw) > maximum_raw:
        raise ValueError("velocity exceeds the selected drive limit")
    return PV.pack(controlword, mode, raw)


@dataclass(frozen=True)
class Frame:
    """Classic CAN bytes and local read observation, not a sample timestamp."""

    can_id: int
    data: bytes
    observed_at: float


@dataclass(frozen=True)
class Feedback:
    """Selected TPDO report; neither command identity nor aperture is present."""

    statusword: int
    mode: int
    velocity_rad_s: float
    observed_at: float

    @property
    def state(self):
        return self.statusword & STATE_MASK


def decode_feedback(frame, node_id, unit_rad_s):
    """Decode statusword, actual mode and actual velocity in little endian."""
    if (not _integer(node_id, 1, 127) or not _finite(unit_rad_s) or
            unit_rad_s <= 0 or frame.can_id != 0x180 + node_id or
            len(frame.data) != PV.size or not _finite(frame.observed_at)):
        raise ValueError("not a complete selected standard-data TPDO")
    status, mode, velocity = PV.unpack(frame.data)
    physical = velocity * unit_rad_s
    if not math.isfinite(physical):
        raise ValueError("feedback scaling overflow")
    return Feedback(status, mode, physical, frame.observed_at)


@dataclass(frozen=True)
class Config:
    """Explicit same-drive profile and finite host-side execution budgets."""

    node_id: int
    velocity_unit_rad_s: float
    maximum_velocity_raw: int
    period_s: float = 0.02
    feedback_timeout_s: float = 0.1
    stop_timeout_s: float = 1.0
    stop_dwell_s: float = 0.05
    stop_velocity_rad_s: float = 0.05
    maximum_frames_per_wait: int = 128

    def validate(self):
        encode_pv(6, 3, 0, self.velocity_unit_rad_s, self.maximum_velocity_raw)
        if (not _integer(self.node_id, 1, 127) or
                not _integer(self.maximum_frames_per_wait, 1, 65535) or
                any(not _finite(x) or x <= 0 for x in
                    (self.period_s, self.feedback_timeout_s,
                     self.stop_timeout_s)) or
                not _finite(self.stop_dwell_s) or self.stop_dwell_s < 0 or
                self.stop_dwell_s >= self.stop_timeout_s or
                not _finite(self.stop_velocity_rad_s) or
                self.stop_velocity_rad_s < 0):
            raise ValueError("invalid host profile or finite budgets")


@dataclass(frozen=True)
class VelocityRequest:
    """One original finite authorization; retransmission never renews it."""

    velocity_rad_s: float
    issued_at: float
    valid_until: float


@dataclass
class Report:
    """Separate software/drive reports; physical motion remains unverified."""

    requested_velocity_rad_s: float = 0
    wire_velocity_rad_s: float = 0
    enabled_confirmed: bool = False
    velocity_frames_sent: int = 0
    quick_stop_sent: bool = False
    quick_stop_state_reported: bool = False
    zero_velocity_dwell_reported: bool = False
    physical_stop_verified: bool = False
    reference_adoption: str = "not_reported_by_profile"
    last_feedback: Feedback | None = None
    error: str | None = None
    stop_error: str | None = None


class _Clock:
    def __init__(self, source):
        self.source = source
        self.previous = None
        self.unchanged = 0

    def __call__(self):
        now = self.source()
        if not _finite(now) or (self.previous is not None and now < self.previous):
            raise RuntimeError("monotonic clock is invalid or regressed")
        self.unchanged = self.unchanged + 1 if now == self.previous else 0
        if self.unchanged > 1024:
            raise RuntimeError("clock and external port made no progress")
        self.previous = now
        return now


def _send(port, config, clock, control, velocity, deadline):
    now = clock()
    if now >= deadline:
        raise TimeoutError("command authority or send budget expired")
    payload = encode_pv(control, 3, velocity, config.velocity_unit_rad_s,
                        config.maximum_velocity_raw)
    port.send(Frame(0x200 + config.node_id, payload, now), deadline)
    if clock() >= deadline:
        raise TimeoutError("send completed outside its original budget")
    return now


def _receive(port, config, clock, since, deadline):
    for _ in range(config.maximum_frames_per_wait):
        if clock() >= deadline:
            return None
        frame = port.receive(deadline)
        now = clock()
        if frame is None:
            if now < deadline:
                raise RuntimeError("CAN port returned no data before its deadline")
            return None
        if now >= deadline:
            return None
        if frame.can_id & ERROR_FLAG:
            raise OSError("CAN controller error frame")
        if frame.can_id != 0x180 + config.node_id:
            continue
        feedback = decode_feedback(frame, config.node_id,
                                   config.velocity_unit_rad_s)
        if feedback.observed_at > now:
            raise RuntimeError("feedback observation is in the future")
        if feedback.observed_at < since:
            continue
        if feedback.state in (0x08, 0x0F):
            raise RuntimeError("drive reported a fault state")
        return feedback
    raise RuntimeError("CAN receive budget exhausted")


def _confirm(port, config, clock, report, state, since, deadline):
    while clock() < deadline:
        feedback = _receive(port, config, clock, since, deadline)
        if feedback is None:
            break
        report.last_feedback = feedback
        if feedback.state == state and feedback.mode == 3:
            return feedback
    raise TimeoutError("actual drive state and mode were not confirmed")


def _stop(port, config, clock, report):
    deadline = clock() + config.stop_timeout_s
    since = _send(port, config, clock, 0x000B, 0, deadline)
    report.quick_stop_sent = True
    quiet_since = None
    last_report = since
    while clock() < deadline:
        feedback = _receive(port, config, clock, since,
                            min(deadline, last_report + config.feedback_timeout_s))
        if feedback is None:
            break
        if feedback.observed_at - last_report > config.feedback_timeout_s:
            raise TimeoutError("stop feedback gap invalidates the observed dwell")
        last_report = feedback.observed_at
        report.last_feedback = feedback
        stopped_report = feedback.state == 0x07 and feedback.mode == 3
        report.quick_stop_state_reported |= stopped_report
        if (stopped_report and abs(feedback.velocity_rad_s) <=
                config.stop_velocity_rad_s):
            if quiet_since is None:
                quiet_since = feedback.observed_at
            if feedback.observed_at - quiet_since >= config.stop_dwell_s:
                report.zero_velocity_dwell_reported = True
                return
        else:
            quiet_since = None
    raise TimeoutError("quick-stop state or reported zero-velocity dwell unconfirmed")


def run_velocity(port, request, config, *, monotonic=time.monotonic):
    """Run one finite session over send(frame, deadline)/receive(deadline).

    The caller owns/joins the port. receive returns a Frame or None at deadline;
    both operations must return within their finite budget. Reported TPDO state
    is checked, but this mapping cannot prove a particular target was adopted.
    """
    config.validate()
    clock = _Clock(monotonic)
    now = clock()
    if (not _finite(request.issued_at) or not _finite(request.valid_until) or
            request.issued_at > now or request.valid_until <= now or
            request.valid_until <= request.issued_at):
        raise ValueError("invalid or expired original velocity authorization")
    encoded = encode_pv(15, 3, request.velocity_rad_s,
                        config.velocity_unit_rad_s, config.maximum_velocity_raw)
    report = Report(requested_velocity_rad_s=request.velocity_rad_s,
                    wire_velocity_rad_s=PV.unpack(encoded)[2] *
                    config.velocity_unit_rad_s)
    try:
        port.send(Frame(0, bytes((1, config.node_id)), now),
                  min(request.valid_until, now + config.feedback_timeout_s))
        for control, state in ((6, 0x21), (7, 0x23), (15, 0x27)):
            deadline = min(request.valid_until,
                           clock() + config.feedback_timeout_s)
            since = _send(port, config, clock, control, 0, deadline)
            _confirm(port, config, clock, report, state, since, deadline)
        report.enabled_confirmed = True
        next_send = clock()
        last_feedback = report.last_feedback.observed_at
        while clock() < request.valid_until:
            now = clock()
            if now >= next_send:
                _send(port, config, clock, 15, request.velocity_rad_s,
                      min(request.valid_until, now + config.feedback_timeout_s))
                report.velocity_frames_sent += 1
                next_send = now + config.period_s
            deadline = min(next_send, request.valid_until,
                           last_feedback + config.feedback_timeout_s)
            feedback = _receive(port, config, clock, last_feedback, deadline)
            if feedback is not None:
                report.last_feedback = feedback
                last_feedback = feedback.observed_at
                if feedback.state != 0x27 or feedback.mode != 3:
                    raise RuntimeError("drive lost enabled Profile Velocity state")
            if clock() - last_feedback >= config.feedback_timeout_s:
                raise TimeoutError("drive feedback observation expired")
    except (OSError, ValueError, RuntimeError) as error:
        report.error = str(error)
    finally:
        try:
            _stop(port, config, clock, report)
        except (OSError, ValueError, RuntimeError) as error:
            report.stop_error = str(error)
    return report


class SocketCAN:
    """Linux classic-CAN application port; explicitly opened by the caller."""

    def __init__(self, interface, node_id, *, monotonic=time.monotonic):
        if (not interface or "\0" in interface or
                len(interface.encode()) >= 16 or not _integer(node_id, 1, 127)):
            raise ValueError("invalid explicit CAN interface or node")
        self.clock = monotonic
        self.socket = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        try:
            selected = struct.pack("=II", 0x180 + node_id,
                                   0x7FF | EXTENDED_FLAG | REMOTE_FLAG)
            self.socket.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, selected)
            self.socket.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                                   struct.pack("=I", 0x1FFFFFFF))
            self.socket.setblocking(False)
            self.socket.bind((interface,))
        except BaseException:
            self.socket.close()
            raise

    def send(self, frame, deadline):
        if (not _integer(frame.can_id, 0, 0x7FF) or len(frame.data) > 8):
            raise ValueError("classic standard-data CAN only")
        remaining = deadline - self.clock()
        if remaining <= 0 or not select.select([], [self.socket], [], remaining)[1]:
            raise TimeoutError("SocketCAN send deadline")
        if self.clock() >= deadline:
            raise TimeoutError("SocketCAN send deadline")
        data = CAN_FRAME.pack(frame.can_id, len(frame.data), frame.data.ljust(8, b"\0"))
        if self.socket.send(data) != CAN_FRAME.size:
            raise OSError("short SocketCAN frame write")

    def receive(self, deadline):
        remaining = deadline - self.clock()
        if remaining <= 0 or not select.select([self.socket], [], [], remaining)[0]:
            return None
        packet = self.socket.recv(CAN_FRAME.size)
        observed = self.clock()
        if len(packet) != CAN_FRAME.size:
            raise OSError("short SocketCAN frame read")
        identifier, length, payload = CAN_FRAME.unpack(packet)
        if length > 8:
            raise OSError("invalid classic CAN payload size")
        return Frame(identifier, payload[:length], observed)

    def close(self):
        self.socket.close()


def main(arguments=None):
    """Default codec demo; only an explicit interface opens a real CAN port."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface")
    parser.add_argument("--node", type=int)
    parser.add_argument("--unit-rad-s", type=float)
    parser.add_argument("--maximum-raw", type=int)
    parser.add_argument("--velocity-rad-s", type=float)
    parser.add_argument("--duration-s", type=float)
    options = parser.parse_args(arguments)
    if options.interface is None:
        print(json.dumps({"scope": "codec_demo_no_hardware", "node_id": 18,
                          "velocity_unit_rad_s": 0.01,
                          "rpdo_hex": encode_pv(15, 3, 2.5, .01, 1000).hex()}))
        return 0
    if any(getattr(options, key) is None for key in
           ("node", "unit_rad_s", "maximum_raw", "velocity_rad_s", "duration_s")):
        parser.error("explicit hardware requires node, scaling, limit, "
                     "velocity and duration")
    if not _finite(options.duration_s) or options.duration_s <= 0:
        parser.error("duration must be positive and finite")
    port = None
    try:
        config = Config(options.node, options.unit_rad_s, options.maximum_raw)
        config.validate()
        now = time.monotonic()
        request = VelocityRequest(options.velocity_rad_s, now, now + options.duration_s)
        encode_pv(15, 3, request.velocity_rad_s, config.velocity_unit_rad_s,
                  config.maximum_velocity_raw)
        port = SocketCAN(options.interface, options.node)
        report = run_velocity(port, request, config)
        print(json.dumps(asdict(report), sort_keys=True))
        return 0 if report.error is None and report.stop_error is None else 1
    except (OSError, ValueError, RuntimeError) as error:
        print(json.dumps({"error": str(error), "physical_stop_verified": False}))
        return 1
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
