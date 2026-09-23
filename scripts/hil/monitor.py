"""Strict serial evidence: time and application progress must agree."""
import ipaddress
import json
import re

PREFIX = 'RAD_HEALTH '
FAULT = re.compile(r'Guru Meditation|panic(?:ked)?|abort\(\)|assert failed|watchdog|task_wdt|Brownout detector|Rebooting', re.I)


class EvidenceError(RuntimeError):
    pass


class Monitor:
    STARTUP_SECONDS = 180
    SOAK_SECONDS = 180
    HEARTBEAT_SECONDS = 20

    def __init__(self, expected, started):
        self.expected = expected
        self.started = started
        self.boot_id = None
        self.markers = set()
        self.last_uptime = None
        self.last_heartbeat = None
        self.soak_started = None
        self.soak_uptime = None
        self.heartbeats = 0
        self.disconnects = None
        self.passed = False

    def tick(self, now):
        if self.soak_started is None and now-self.started > self.STARTUP_SECONDS:
            raise EvidenceError('startup deadline exceeded')
        if self.last_heartbeat is not None and now-self.last_heartbeat > self.HEARTBEAT_SECONDS:
            raise EvidenceError('missing application heartbeat')

    def feed(self, line, now):
        self.tick(now)
        if FAULT.search(line):
            raise EvidenceError('device reported a crash, watchdog or brownout')
        for marker in ('rst:0x', 'esp-rom:'):
            if marker in line.lower():
                if self.boot_id is not None or marker in self.markers:
                    raise EvidenceError('unexpected reboot')
                self.markers.add(marker)
        if PREFIX not in line:
            return
        try:
            record = json.loads(line.split(PREFIX, 1)[1])
        except ValueError as error:
            raise EvidenceError('malformed health record') from error
        if not isinstance(record, dict):
            raise EvidenceError('health record must be an object')
        boot, uptime = record.get('boot_id'), record.get('uptime_ms')
        if type(boot) is not int or not 0 <= boot <= 0xffffffff or type(uptime) is not int or uptime < 0:
            raise EvidenceError('invalid boot identity or uptime')
        if record.get('event') == 'boot':
            if self.boot_id is not None:
                raise EvidenceError('second boot; observation cannot restart')
            if any(record.get(k) != v for k, v in self.expected.items()):
                raise EvidenceError('running firmware or device identity mismatch')
            self.boot_id = boot
            self.last_uptime = uptime
            self.last_heartbeat = now
            return
        if self.boot_id is None or boot != self.boot_id:
            raise EvidenceError('missing boot evidence or changed boot ID')
        if record.get('event') != 'heartbeat':
            raise EvidenceError('unknown health event')
        if uptime <= self.last_uptime:
            raise EvidenceError('stalled or decreased uptime')
        self.last_uptime = uptime
        self.last_heartbeat = now
        self.heartbeats += 1
        disconnects = record.get('disconnects')
        if type(disconnects) is not int or disconnects < 0:
            raise EvidenceError('invalid network continuity counter')
        if self.soak_started is not None and disconnects != self.disconnects:
            raise EvidenceError('Wi-Fi disconnected during observation')
        self.disconnects = disconnects
        try:
            ip = ipaddress.IPv4Address(record.get('ip', ''))
            valid_ip = not (ip.is_unspecified or ip.is_loopback or ip.is_multicast or ip.is_link_local or str(ip) == '255.255.255.255')
        except ipaddress.AddressValueError:
            valid_ip = False
        app_age = record.get('app_age_ms')
        network_age = record.get('network_age_ms')
        ready = (record.get('ready') is True and record.get('wifi') is True
                 and record.get('bench_wifi') is True and valid_ip
                 and type(app_age) is int and 0 <= app_age <= 20000
                 and record.get('internet') is True
                 and type(network_age) is int and 0 <= network_age <= 60000)
        if self.soak_started is not None and not ready:
            raise EvidenceError('application readiness or network connectivity lost')
        if self.soak_started is None and ready:
            self.soak_started, self.soak_uptime = now, uptime
        if (self.soak_started is not None and now-self.soak_started >= self.SOAK_SECONDS
                and uptime-self.soak_uptime >= self.SOAK_SECONDS*1000):
            self.passed = True

    def result(self, now):
        return dict(self.expected, status='passed' if self.passed else 'failed',
                    boot_id=self.boot_id, heartbeats=self.heartbeats,
                    soak_seconds=0 if self.soak_started is None else now-self.soak_started,
                    device_soak_ms=0 if self.soak_uptime is None else self.last_uptime-self.soak_uptime)
