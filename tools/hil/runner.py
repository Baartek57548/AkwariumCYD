#!/usr/bin/env python3
"""Hardware-in-the-loop checks for an AquaCYD controller.

The runner uses only the Python standard library. Physical serial verification is
enabled when pyserial is installed and AQUACYD_HIL_SERIAL_PORT is configured.
Without a configured controller every hardware test is reported as SKIP.
"""

from __future__ import annotations

import argparse
import dataclasses
import enum
import http.server
import json
import os
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from collections.abc import Callable
from typing import Any


class SkipTest(RuntimeError):
    """A test cannot run because an explicit laboratory prerequisite is absent."""


class TestFailure(RuntimeError):
    """A HIL contract or safety assertion failed."""


class Outcome(enum.Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"


@dataclasses.dataclass(frozen=True)
class Result:
    name: str
    outcome: Outcome
    detail: str
    duration_seconds: float


@dataclasses.dataclass(frozen=True)
class Config:
    base_url: str
    bearer_token: str
    lab_token: str
    admin_pin: str
    timeout_seconds: float
    auth_path: str
    capabilities_path: str
    command_path: str
    status_path: str
    health_path: str
    override_target: str
    override_state_path: str
    override_ttl_seconds: int
    override_grace_seconds: float
    allow_mutations: bool
    wifi_cut_url: str
    wifi_restore_url: str
    wifi_outage_seconds: float
    reconnect_timeout_seconds: float
    ota_health_path: str
    ota_rollback_url: str
    allow_ota_rollback: bool
    aquael_trace_url: str
    aquael_transition_timeout_seconds: float
    serial_port: str
    serial_baud: int
    serial_boot_marker: str

    @classmethod
    def from_environment(cls) -> "Config":
        return cls(
            base_url=_clean_url(os.getenv("AQUACYD_HIL_BASE_URL", "")),
            bearer_token=_token_env("AQUACYD_HIL_TOKEN"),
            lab_token=os.getenv("AQUACYD_HIL_LAB_TOKEN", "").strip(),
            admin_pin=os.getenv("AQUACYD_HIL_ADMIN_PIN", "").strip(),
            timeout_seconds=_float_env("AQUACYD_HIL_TIMEOUT_SECONDS", 5.0, 0.2, 60.0),
            auth_path=_path_env("AQUACYD_HIL_AUTH_PATH", "/api/v2/auth"),
            capabilities_path=_path_env(
                "AQUACYD_HIL_CAPABILITIES_PATH", "/api/v2/capabilities"
            ),
            command_path=_path_env("AQUACYD_HIL_COMMAND_PATH", "/api/action"),
            status_path=_path_env("AQUACYD_HIL_STATUS_PATH", "/api/status"),
            health_path=_path_env("AQUACYD_HIL_HEALTH_PATH", "/api/status"),
            override_target=os.getenv("AQUACYD_HIL_OVERRIDE_TARGET", "filter").strip(),
            override_state_path=os.getenv(
                "AQUACYD_HIL_OVERRIDE_STATE_PATH", "controlState.overrides"
            ).strip(),
            override_ttl_seconds=_int_env(
                "AQUACYD_HIL_OVERRIDE_TTL_SECONDS", 30, 1, 120
            ),
            override_grace_seconds=_float_env(
                "AQUACYD_HIL_OVERRIDE_GRACE_SECONDS", 2.0, 0.1, 30.0
            ),
            allow_mutations=_bool_env("AQUACYD_HIL_ALLOW_MUTATIONS"),
            wifi_cut_url=_clean_url(os.getenv("AQUACYD_HIL_WIFI_CUT_URL", "")),
            wifi_restore_url=_clean_url(
                os.getenv("AQUACYD_HIL_WIFI_RESTORE_URL", "")
            ),
            wifi_outage_seconds=_float_env(
                "AQUACYD_HIL_WIFI_OUTAGE_SECONDS", 2.0, 0.2, 30.0
            ),
            reconnect_timeout_seconds=_float_env(
                "AQUACYD_HIL_RECONNECT_TIMEOUT_SECONDS", 30.0, 1.0, 300.0
            ),
            ota_health_path=_optional_path_env("AQUACYD_HIL_OTA_HEALTH_PATH"),
            ota_rollback_url=_clean_url(
                os.getenv("AQUACYD_HIL_OTA_ROLLBACK_URL", "")
            ),
            allow_ota_rollback=_bool_env("AQUACYD_HIL_ALLOW_OTA_ROLLBACK"),
            aquael_trace_url=_clean_url(
                os.getenv("AQUACYD_HIL_AQUAEL_TRACE_URL", "")
            ),
            aquael_transition_timeout_seconds=_float_env(
                "AQUACYD_HIL_AQUAEL_TRANSITION_TIMEOUT_SECONDS",
                20.0,
                2.0,
                120.0,
            ),
            serial_port=os.getenv("AQUACYD_HIL_SERIAL_PORT", "").strip(),
            serial_baud=_int_env("AQUACYD_HIL_SERIAL_BAUD", 115200, 1200, 4_000_000),
            serial_boot_marker=os.getenv(
                "AQUACYD_HIL_SERIAL_BOOT_MARKER", "AquaCYD"
            ).strip(),
        )


def _bool_env(name: str) -> bool:
    return os.getenv(name, "").strip().lower() in {"1", "true", "yes", "on"}


def _token_env(name: str) -> str:
    value = os.getenv(name, "").strip().lower()
    if value and (
        len(value) != 32
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise ValueError(f"{name} must be empty or exactly 32 hexadecimal characters")
    return value


def _float_env(name: str, default: float, minimum: float, maximum: float) -> float:
    raw = os.getenv(name, "").strip()
    try:
        value = default if not raw else float(raw)
    except ValueError as error:
        raise ValueError(f"{name} must be numeric") from error
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _int_env(name: str, default: int, minimum: int, maximum: int) -> int:
    raw = os.getenv(name, "").strip()
    try:
        value = default if not raw else int(raw)
    except ValueError as error:
        raise ValueError(f"{name} must be an integer") from error
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _path_env(name: str, default: str) -> str:
    value = os.getenv(name, default).strip()
    if not value.startswith("/") or "://" in value:
        raise ValueError(f"{name} must be an absolute URL path")
    return value


def _optional_path_env(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        return ""
    if not value.startswith("/") or "://" in value:
        raise ValueError(f"{name} must be empty or an absolute URL path")
    return value


def _clean_url(value: str) -> str:
    return value.strip().rstrip("/")


class JsonClient:
    def __init__(self, config: Config) -> None:
        self._config = config
        self._device_token = config.bearer_token

    @property
    def device_token(self) -> str:
        return self._device_token

    def invalidate_session(self) -> None:
        self._device_token = ""

    def ensure_session(self) -> str:
        if self._device_token:
            return self._device_token
        if not self._config.admin_pin:
            return ""
        status, body = self.request(
            "POST",
            self._config.auth_path,
            {"pin": self._config.admin_pin},
            encoding="form",
        )
        if status != 200:
            message = body.get("message") or body.get("code") or "authentication failed"
            raise TestFailure(f"v2 authentication returned HTTP {status}: {message}")
        token = _nested_optional(body, "data.sessionToken")
        if not isinstance(token, str) or len(token) != 32:
            raise TestFailure("v2 authentication did not return a 32-character token")
        self._device_token = token
        return token

    def url(self, path_or_url: str) -> str:
        if path_or_url.startswith(("http://", "https://")):
            return path_or_url
        if not self._config.base_url:
            raise SkipTest("AQUACYD_HIL_BASE_URL is not configured")
        return urllib.parse.urljoin(f"{self._config.base_url}/", path_or_url.lstrip("/"))

    def request(
        self,
        method: str,
        path_or_url: str,
        payload: dict[str, Any] | None = None,
        *,
        include_device_auth: bool = True,
        encoding: str = "json",
    ) -> tuple[int, dict[str, Any]]:
        headers = {"Accept": "application/json", "User-Agent": "AquaCYD-HIL/1"}
        if payload is not None:
            headers["Content-Type"] = (
                "application/x-www-form-urlencoded"
                if encoding == "form"
                else "application/json"
            )
        if include_device_auth and self._device_token:
            headers["Authorization"] = f"Bearer {self._device_token}"
        if not include_device_auth and self._config.lab_token:
            headers["Authorization"] = f"Bearer {self._config.lab_token}"
        if payload is None:
            body = None
        elif encoding == "form":
            body = urllib.parse.urlencode(
                {
                    key: (
                        "true"
                        if value is True
                        else "false"
                        if value is False
                        else str(value)
                    )
                    for key, value in payload.items()
                }
            ).encode("utf-8")
        elif encoding == "json":
            body = json.dumps(payload).encode("utf-8")
        else:
            raise ValueError(f"unsupported request encoding: {encoding}")
        request = urllib.request.Request(
            self.url(path_or_url), data=body, headers=headers, method=method
        )
        attempts = 2 if method in {"GET", "HEAD"} else 1
        for attempt in range(attempts):
            try:
                with urllib.request.urlopen(
                    request, timeout=self._config.timeout_seconds
                ) as response:
                    status = response.status
                    raw = response.read(1_048_577)
                break
            except urllib.error.HTTPError as error:
                status = error.code
                raw = error.read(1_048_577)
                break
            except (OSError, urllib.error.URLError):
                if attempt + 1 >= attempts:
                    raise
                time.sleep(0.05)
        if len(raw) > 1_048_576:
            raise TestFailure("controller response exceeds the 1 MiB safety limit")
        if not raw:
            return status, {}
        try:
            decoded = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise TestFailure(f"HTTP {status} returned invalid JSON") from error
        if not isinstance(decoded, dict):
            raise TestFailure(f"HTTP {status} JSON root must be an object")
        return status, decoded

    def require_json(
        self,
        method: str,
        path_or_url: str,
        payload: dict[str, Any] | None = None,
        *,
        accepted: set[int] | None = None,
        include_device_auth: bool = True,
        encoding: str = "json",
    ) -> dict[str, Any]:
        status, body = self.request(
            method,
            path_or_url,
            payload,
            include_device_auth=include_device_auth,
            encoding=encoding,
        )
        expected = accepted or {200}
        if status not in expected:
            message = body.get("message") or body.get("error") or "unexpected response"
            raise TestFailure(f"HTTP {status}: {message}")
        return body


class HilSuite:
    def __init__(self, config: Config, *, dry_run: bool = False) -> None:
        self.config = config
        self.client = JsonClient(config)
        self.dry_run = dry_run
        self.results: list[Result] = []

    def run(self) -> int:
        checks: list[tuple[str, Callable[[], str]]] = [
            ("controller health", self.controller_health),
            ("duplicate command idempotency", self.duplicate_command),
            ("temporary override timeout", self.override_timeout),
            ("Wi-Fi outage and reconnect", self.wifi_reconnect),
            ("OTA health contract", self.ota_health),
            ("OTA rollback", self.ota_rollback),
            ("dual Aquael API profiles", self.aquael_api_profiles),
            ("dual Aquael electrical sequence", self.aquael_light_sequence),
            ("serial boot marker", self.serial_boot_marker),
        ]
        for name, check in checks:
            self._run_one(name, check)
        self._print_summary()
        return 1 if any(result.outcome is Outcome.FAIL for result in self.results) else 0

    def _run_one(self, name: str, check: Callable[[], str]) -> None:
        started = time.monotonic()
        if self.dry_run:
            detail = self._dry_run_detail(name)
            outcome = Outcome.SKIP if detail.startswith("missing:") else Outcome.PASS
        else:
            try:
                detail = check()
                outcome = Outcome.PASS
            except SkipTest as error:
                detail = str(error)
                outcome = Outcome.SKIP
            except (TestFailure, OSError, TimeoutError) as error:
                detail = str(error)
                outcome = Outcome.FAIL
            except Exception as error:  # pragma: no cover - defensive HIL boundary
                detail = f"unexpected {type(error).__name__}: {error}"
                outcome = Outcome.FAIL
        duration = time.monotonic() - started
        result = Result(name, outcome, detail, duration)
        self.results.append(result)
        print(f"[{outcome.value}] {name}: {detail} ({duration:.2f}s)")

    def _dry_run_detail(self, name: str) -> str:
        if not self.config.base_url:
            return "missing: AQUACYD_HIL_BASE_URL; hardware access would be skipped"
        mutation_tests = {
            "duplicate command idempotency",
            "temporary override timeout",
            "Wi-Fi outage and reconnect",
            "OTA rollback",
            "dual Aquael API profiles",
            "dual Aquael electrical sequence",
        }
        if name in mutation_tests and not self.config.allow_mutations:
            return "missing: AQUACYD_HIL_ALLOW_MUTATIONS=1"
        if (
            name in mutation_tests
            and not self.config.bearer_token
            and not self.config.admin_pin
        ):
            return "missing: AQUACYD_HIL_ADMIN_PIN or a fresh AQUACYD_HIL_TOKEN"
        if name == "Wi-Fi outage and reconnect" and (
            not self.config.wifi_cut_url or not self.config.wifi_restore_url
        ):
            return "missing: Wi-Fi cut and restore URLs"
        if name == "OTA rollback" and (
            not self.config.allow_ota_rollback
            or not self.config.ota_rollback_url
            or not self.config.ota_health_path
        ):
            return (
                "missing: explicit OTA rollback permission, trigger URL "
                "and full health path"
            )
        if (
            name == "dual Aquael electrical sequence"
            and not self.config.aquael_trace_url
        ):
            return "missing: AQUACYD_HIL_AQUAEL_TRACE_URL"
        if name == "serial boot marker" and not self.config.serial_port:
            return "missing: AQUACYD_HIL_SERIAL_PORT"
        return "configuration valid; no network or serial operation executed"

    def _require_mutations(self) -> None:
        if not self.config.base_url:
            raise SkipTest("AQUACYD_HIL_BASE_URL is not configured")
        if not self.config.allow_mutations:
            raise SkipTest("set AQUACYD_HIL_ALLOW_MUTATIONS=1 to permit state changes")
        self.client.ensure_session()

    def controller_health(self) -> str:
        if not self.config.base_url:
            raise SkipTest("AQUACYD_HIL_BASE_URL is not configured")
        body = self.client.require_json(
            "GET", self.config.health_path, accepted={200, 204}
        )
        if body and body.get("healthy") is False:
            raise TestFailure("controller reports unhealthy state")
        return "controller accepted an authenticated health request"

    def duplicate_command(self) -> str:
        self._require_mutations()
        command_id = str(uuid.uuid4())
        first_status, first = self._send_override(
            command_id=command_id,
            state=True,
            duration_seconds=self.config.override_ttl_seconds,
        )
        if first_status not in {200, 201, 202}:
            raise TestFailure(f"first command returned HTTP {first_status}")
        second_status, second = self._send_override(
            command_id=command_id,
            state=True,
            duration_seconds=self.config.override_ttl_seconds,
        )
        if second_status not in {200, 202, 208, 409}:
            raise TestFailure(f"duplicate command returned HTTP {second_status}")
        response_id = second.get("id") or second.get("commandId")
        if response_id and response_id != command_id:
            raise TestFailure("duplicate response refers to a different command")
        duplicate_marker = (
            second_status in {208, 409}
            or second.get("duplicate") is True
            or second.get("deduplicated") is True
            or second.get("replayed") is True
            or str(second.get("status", "")).lower() in {"duplicate", "replayed"}
        )
        if not duplicate_marker:
            first_result = first.get("result") or first.get("state")
            second_result = second.get("result") or second.get("state")
            if first_result is None or first_result != second_result:
                raise TestFailure("API did not prove that the duplicate was deduplicated")
        return f"command {command_id} was applied at most once"

    def override_timeout(self) -> str:
        self._require_mutations()
        status_code, response = self._send_override(
            command_id=str(uuid.uuid4()),
            state=True,
            duration_seconds=self.config.override_ttl_seconds,
        )
        if status_code not in {200, 201, 202}:
            raise TestFailure(
                f"override command returned HTTP {status_code}: "
                f"{response.get('message') or response.get('error') or 'unknown error'}"
            )
        deadline = (
            time.monotonic()
            + self.config.override_ttl_seconds
            + self.config.override_grace_seconds
        )
        saw_active = False
        last_value: Any = None
        while time.monotonic() < deadline:
            status = self.client.require_json("GET", self.config.status_path)
            last_value = _nested_value(status, self.config.override_state_path)
            active = _is_override_active(last_value, self.config.override_target)
            saw_active = saw_active or active
            if saw_active and not active:
                return "temporary override returned to automatic control"
            time.sleep(0.25)
        if not saw_active:
            raise TestFailure(
                f"override never became active at {self.config.override_state_path}"
            )
        raise TestFailure(f"override remained active after its TTL: {last_value!r}")

    def _send_override(
        self,
        *,
        command_id: str,
        state: bool,
        duration_seconds: int,
    ) -> tuple[int, dict[str, Any]]:
        return self._send_v2_action(
            "set_timed_override",
            command_id=command_id,
            arguments={
                "target": self.config.override_target,
                "state": state,
                "durationSec": duration_seconds,
            },
        )

    def _send_v2_action(
        self,
        action: str,
        *,
        command_id: str,
        arguments: dict[str, Any],
        retry_auth: bool = True,
    ) -> tuple[int, dict[str, Any]]:
        payload: dict[str, Any] = {
            "action": action,
            "commandId": command_id,
            **arguments,
        }
        token = self.client.ensure_session()
        if token:
            payload["token"] = token
        elif self.config.admin_pin:
            payload["pin"] = self.config.admin_pin
        status, body = self.client.request(
            "POST",
            self.config.command_path,
            payload,
            encoding="form",
        )
        if (
            retry_auth
            and status == 401
            and self.config.admin_pin
            and str(body.get("code", "")) in {"session_required", "session_expired"}
        ):
            self.client.invalidate_session()
            return self._send_v2_action(
                action,
                command_id=command_id,
                arguments=arguments,
                retry_auth=False,
            )
        return status, body

    def wifi_reconnect(self) -> str:
        self._require_mutations()
        if not self.config.wifi_cut_url or not self.config.wifi_restore_url:
            raise SkipTest(
                "configure AQUACYD_HIL_WIFI_CUT_URL and "
                "AQUACYD_HIL_WIFI_RESTORE_URL for a managed AP/relay"
            )
        self.client.require_json(
            "POST",
            self.config.wifi_cut_url,
            {"state": "off"},
            accepted={200, 202, 204},
            include_device_auth=False,
        )
        outage_observed = False
        try:
            outage_deadline = time.monotonic() + self.config.wifi_outage_seconds
            while time.monotonic() < outage_deadline:
                try:
                    status, _ = self.client.request("GET", self.config.health_path)
                    outage_observed = outage_observed or status >= 500
                except (OSError, urllib.error.URLError, socket.timeout):
                    outage_observed = True
                time.sleep(0.2)
        finally:
            self.client.require_json(
                "POST",
                self.config.wifi_restore_url,
                {"state": "on"},
                accepted={200, 202, 204},
                include_device_auth=False,
            )
        if not outage_observed:
            raise TestFailure("managed outage did not make the controller unavailable")
        reconnect_deadline = (
            time.monotonic() + self.config.reconnect_timeout_seconds
        )
        while time.monotonic() < reconnect_deadline:
            try:
                status, body = self.client.request("GET", self.config.health_path)
                if status in {200, 204} and body.get("healthy", True) is not False:
                    return "controller recovered after the managed Wi-Fi outage"
            except (OSError, urllib.error.URLError, socket.timeout):
                pass
            time.sleep(0.5)
        raise TestFailure("controller did not reconnect before the configured deadline")

    def ota_health(self) -> str:
        if not self.config.base_url:
            raise SkipTest("AQUACYD_HIL_BASE_URL is not configured")
        capabilities_status, capabilities = self.client.request(
            "GET", self.config.capabilities_path
        )
        if capabilities_status != 200:
            raise TestFailure(
                f"v2 capabilities returned HTTP {capabilities_status}"
            )
        features = _nested_optional(capabilities, "data.features")
        if not isinstance(features, dict) or features.get("safeOta") is not True:
            raise TestFailure("v2 capabilities do not advertise safeOta")
        ota = _nested_optional(capabilities, "data.ota")
        if not isinstance(ota, dict):
            raise TestFailure("v2 capabilities do not contain data.ota")
        required_capabilities = {
            "rollbackAvailable",
            "updatePartitionBytes",
            "pendingVerify",
            "state",
        }
        missing_capabilities = sorted(required_capabilities.difference(ota))
        if missing_capabilities:
            raise TestFailure(
                f"OTA capabilities miss: {', '.join(missing_capabilities)}"
            )
        if not isinstance(ota["rollbackAvailable"], bool):
            raise TestFailure("OTA rollbackAvailable must be boolean")
        if not isinstance(ota["pendingVerify"], bool):
            raise TestFailure("OTA pendingVerify must be boolean")
        if (
            not isinstance(ota["updatePartitionBytes"], int)
            or ota["updatePartitionBytes"] <= 0
        ):
            raise TestFailure("OTA updatePartitionBytes must be a positive integer")
        if not isinstance(ota["state"], str) or not ota["state"].strip():
            raise TestFailure("OTA state must be a non-empty string")
        if ota["pendingVerify"] and not ota["rollbackAvailable"]:
            raise TestFailure("a pending image has no rollback partition")

        if not self.config.ota_health_path:
            return "firmware exposes safe OTA state through v2 capabilities"
        body = self.client.require_json("GET", self.config.ota_health_path)
        self._validate_full_ota_health(body)
        return "OTA capabilities and full boot health are internally consistent"

    @staticmethod
    def _validate_full_ota_health(body: dict[str, Any]) -> None:
        required = {"healthy", "bootSlot", "rollbackAvailable", "pendingValidation"}
        missing = sorted(required.difference(body))
        if missing:
            raise TestFailure(f"OTA health response misses: {', '.join(missing)}")
        if not isinstance(body["healthy"], bool):
            raise TestFailure("OTA healthy must be boolean")
        if not isinstance(body["rollbackAvailable"], bool):
            raise TestFailure("OTA rollbackAvailable must be boolean")
        if not isinstance(body["pendingValidation"], bool):
            raise TestFailure("OTA pendingValidation must be boolean")
        if not isinstance(body["bootSlot"], (str, int)):
            raise TestFailure("OTA bootSlot must be a string or integer")
        if body["pendingValidation"] and not body["rollbackAvailable"]:
            raise TestFailure("a pending image has no rollback slot")

    def ota_rollback(self) -> str:
        self._require_mutations()
        if not self.config.allow_ota_rollback:
            raise SkipTest("set AQUACYD_HIL_ALLOW_OTA_ROLLBACK=1 for destructive rollback")
        if not self.config.ota_rollback_url:
            raise SkipTest("AQUACYD_HIL_OTA_ROLLBACK_URL is not configured")
        if not self.config.ota_health_path:
            raise SkipTest(
                "AQUACYD_HIL_OTA_HEALTH_PATH is required to verify boot-slot rollback"
            )
        before = self.client.require_json("GET", self.config.ota_health_path)
        self._validate_full_ota_health(before)
        if before.get("rollbackAvailable") is not True:
            raise SkipTest("controller reports that no rollback image is available")
        previous_slot = before.get("bootSlot")
        self.client.require_json(
            "POST",
            self.config.ota_rollback_url,
            {"reason": "hil_validation", "confirm": True},
            accepted={200, 202, 204},
        )
        deadline = time.monotonic() + self.config.reconnect_timeout_seconds
        while time.monotonic() < deadline:
            try:
                after = self.client.require_json("GET", self.config.ota_health_path)
                self._validate_full_ota_health(after)
                if (
                    after.get("healthy") is True
                    and after.get("pendingValidation") is False
                    and after.get("bootSlot") != previous_slot
                ):
                    return (
                        f"rollback changed boot slot from {previous_slot!r} "
                        f"to {after.get('bootSlot')!r}"
                    )
            except (OSError, urllib.error.URLError, socket.timeout, TestFailure):
                pass
            time.sleep(0.5)
        raise TestFailure("rollback did not produce a healthy previous boot slot")

    def aquael_api_profiles(self) -> str:
        self._require_mutations()
        capabilities = self.client.require_json(
            "GET", self.config.capabilities_path
        )
        feature = _nested_optional(
            capabilities, "data.features.aquaelLightProfiles"
        )
        targets = _nested_optional(capabilities, "data.lightTargets")
        if feature is not True or targets != ["front", "rear"]:
            raise TestFailure(
                "firmware must advertise aquaelLightProfiles for front and rear"
            )
        limits = _nested_optional(capabilities, "data.limits")
        if not isinstance(limits, dict):
            raise TestFailure("v2 capabilities do not contain Aquael timing limits")
        expected_limits = {
            "lightProfileToggleMaxOffMs": 5000,
            "lightResetThresholdMs": 5000,
            "lightCycleOffMs": 1000,
            "lightCalibrationOffMs": 6000,
        }
        mismatched = {
            key: limits.get(key)
            for key, expected in expected_limits.items()
            if limits.get(key) != expected
        }
        if mismatched:
            raise TestFailure(
                f"firmware advertises unsafe Aquael timing limits: {mismatched}"
            )

        override_seconds = max(
            30,
            min(
                120,
                int(self.config.aquael_transition_timeout_seconds * 4),
            ),
        )
        primary_failure: BaseException | None = None
        try:
            for target in ("light1", "light2"):
                status, body = self._send_v2_action(
                    "set_timed_override",
                    command_id=f"hil-{uuid.uuid4().hex[:20]}",
                    arguments={
                        "target": target,
                        "state": True,
                        "durationSec": override_seconds,
                    },
                )
                self._require_action_response(status, body, "light override")

            stages = (
                {"front": "day", "rear": "day"},
                {"front": "daybreak", "rear": "night"},
                {"front": "night", "rear": "daybreak"},
                {"front": "day", "rear": "day"},
            )
            for expected in stages:
                for target, profile in expected.items():
                    status, body = self._send_v2_action(
                        "set_light_profile",
                        command_id=f"hil-{uuid.uuid4().hex[:20]}",
                        arguments={"target": target, "profile": profile},
                    )
                    self._require_action_response(
                        status,
                        body,
                        f"{target} profile {profile}",
                    )
                self._wait_for_light_profiles(expected)
        except BaseException as error:
            primary_failure = error
            raise
        finally:
            cleanup_failures = self._clear_aquael_test_overrides()
            if cleanup_failures and primary_failure is None:
                raise TestFailure(
                    "Aquael test could not restore AUTO: "
                    + "; ".join(cleanup_failures)
                )
        return (
            "front and rear accepted independent DAY/DAYBREAK/NIGHT profiles "
            "through the real v2 action contract"
        )

    @staticmethod
    def _require_action_response(
        status: int,
        body: dict[str, Any],
        operation: str,
    ) -> None:
        if status != 200 or body.get("ok") is not True:
            detail = body.get("message") or body.get("code") or "unknown error"
            raise TestFailure(f"{operation} failed with HTTP {status}: {detail}")
        command_id = body.get("commandId")
        if not isinstance(command_id, str) or not command_id:
            raise TestFailure(f"{operation} response has no commandId")

    def _wait_for_light_profiles(self, expected: dict[str, str]) -> None:
        deadline = (
            time.monotonic() + self.config.aquael_transition_timeout_seconds
        )
        last_lights: Any = None
        while time.monotonic() < deadline:
            status = self.client.require_json("GET", self.config.status_path)
            last_lights = status.get("lights")
            if isinstance(last_lights, dict):
                ready = True
                for target, profile in expected.items():
                    light = last_lights.get(target)
                    if (
                        not isinstance(light, dict)
                        or light.get("profile") != profile
                        or light.get("known") is not True
                        or light.get("transitioning") is not False
                        or light.get("on") is not True
                    ):
                        ready = False
                        break
                if ready:
                    return
            time.sleep(0.2)
        raise TestFailure(
            f"Aquael profiles did not settle before timeout: {last_lights!r}"
        )

    def _clear_aquael_test_overrides(self) -> list[str]:
        failures: list[str] = []
        for target in ("light1", "light2"):
            try:
                status, body = self._send_v2_action(
                    "clear_timed_override",
                    command_id=f"hil-{uuid.uuid4().hex[:20]}",
                    arguments={"target": target},
                )
                if status != 200 or body.get("ok") is not True:
                    failures.append(
                        f"{target}: HTTP {status} "
                        f"{body.get('code') or body.get('message') or 'unknown error'}"
                    )
            except (OSError, TestFailure) as error:
                failures.append(f"{target}: {error}")
        return failures

    def aquael_light_sequence(self) -> str:
        self._require_mutations()
        if not self.config.aquael_trace_url:
            raise SkipTest(
                "configure AQUACYD_HIL_AQUAEL_TRACE_URL for the isolated "
                "relay/photodiode fixture"
            )
        run_id = str(uuid.uuid4())
        trace = self.client.require_json(
            "POST",
            self.config.aquael_trace_url,
            {
                "operation": "exercise_aquael_daynight",
                "runId": run_id,
                "targets": ["front", "rear"],
                "profiles": ["day", "daybreak", "night", "day"],
                "shortOffMaxMs": 5000,
                "resetOffMinMs": 5001,
            },
            accepted={200, 202},
            include_device_auth=False,
        )
        response_run_id = trace.get("runId")
        if response_run_id not in {None, run_id}:
            raise TestFailure("Aquael trace belongs to a different HIL run")
        conflicts = trace.get("conflicts")
        if conflicts != []:
            raise TestFailure("fixture detected overlapping contradictory commands")
        events = trace.get("events")
        if not isinstance(events, list):
            raise TestFailure("Aquael trace must contain an events array")
        seen_command_ids: set[str] = set()
        for event in events:
            if not isinstance(event, dict):
                raise TestFailure("Aquael trace contains a non-object event")
            if str(event.get("target", "")).lower() not in {"front", "rear"}:
                raise TestFailure("Aquael trace contains an unexpected relay target")
            command_id = str(event.get("commandId", "")).strip()
            if not command_id or command_id in seen_command_ids:
                raise TestFailure(
                    "Aquael trace must use one globally unique commandId per edge"
                )
            seen_command_ids.add(command_id)
        for target in ("front", "rear"):
            _validate_aquael_events(events, target)
        return (
            "front and rear independently followed "
            "DAY -> DAYBREAK -> NIGHT -> DAY with safe relay timing"
        )

    def serial_boot_marker(self) -> str:
        if not self.config.serial_port:
            raise SkipTest("AQUACYD_HIL_SERIAL_PORT is not configured")
        try:
            import serial  # type: ignore[import-not-found]
        except ImportError as error:
            raise SkipTest("install pyserial to enable serial verification") from error
        deadline = time.monotonic() + self.config.timeout_seconds
        lines: list[str] = []
        with serial.Serial(
            self.config.serial_port,
            self.config.serial_baud,
            timeout=0.25,
        ) as port:
            while time.monotonic() < deadline:
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                lines.append(line)
                if self.config.serial_boot_marker in line:
                    return f"observed serial marker {self.config.serial_boot_marker!r}"
                if len(lines) > 100:
                    lines.pop(0)
        raise TestFailure(
            f"serial marker {self.config.serial_boot_marker!r} was not observed"
        )

    def _print_summary(self) -> None:
        counts = {
            outcome: sum(result.outcome is outcome for result in self.results)
            for outcome in Outcome
        }
        print(
            "HIL summary: "
            f"{counts[Outcome.PASS]} passed, "
            f"{counts[Outcome.FAIL]} failed, "
            f"{counts[Outcome.SKIP]} skipped"
        )


def _nested_value(payload: dict[str, Any], dotted_path: str) -> Any:
    current: Any = payload
    for part in dotted_path.split("."):
        if not isinstance(current, dict) or part not in current:
            raise TestFailure(f"status does not contain {dotted_path}")
        current = current[part]
    return current


def _nested_optional(payload: dict[str, Any], dotted_path: str) -> Any:
    current: Any = payload
    for part in dotted_path.split("."):
        if not isinstance(current, dict) or part not in current:
            return None
        current = current[part]
    return current


def _is_override_active(value: Any, target: str) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, list):
        for entry in value:
            if (
                isinstance(entry, dict)
                and str(entry.get("target", "")).lower() == target.lower()
            ):
                remaining = entry.get("remainingSec")
                return not isinstance(remaining, (int, float)) or remaining > 0
        return False
    if isinstance(value, dict):
        if "active" in value:
            return value["active"] is True
        value = value.get("mode")
    return str(value).strip().lower() in {
        "on",
        "off",
        "override",
        "manual",
        "forced_on",
        "forced_off",
    }


def _validate_aquael_events(events: list[Any], target: str) -> None:
    normalized: list[tuple[int, bool, str, str]] = []
    command_ids: set[str] = set()
    for raw in events:
        if not isinstance(raw, dict) or str(raw.get("target", "")).lower() != target:
            continue
        timestamp = raw.get("atMs")
        state = raw.get("state")
        command_id = str(raw.get("commandId", "")).strip()
        profile = str(raw.get("profile", "")).strip().lower()
        if not isinstance(timestamp, int) or timestamp < 0:
            raise TestFailure(f"{target} trace contains an invalid timestamp")
        if not isinstance(state, bool):
            raise TestFailure(f"{target} trace contains a non-boolean relay state")
        if not command_id:
            raise TestFailure(f"{target} trace event has no commandId")
        if command_id in command_ids:
            raise TestFailure(f"{target} trace reuses commandId {command_id}")
        command_ids.add(command_id)
        normalized.append((timestamp, state, profile, command_id))
    normalized.sort(key=lambda item: item[0])
    if len(normalized) != 8:
        raise TestFailure(
            f"{target} trace must contain four OFF/ON pairs; got {len(normalized)} events"
        )
    profiles: list[str] = []
    off_durations: list[int] = []
    previous_timestamp = -1
    for index in range(0, len(normalized), 2):
        off_event = normalized[index]
        on_event = normalized[index + 1]
        if off_event[0] <= previous_timestamp or on_event[0] <= off_event[0]:
            raise TestFailure(f"{target} relay events are not strictly ordered")
        if off_event[1] is not False or on_event[1] is not True:
            raise TestFailure(f"{target} trace contains contradictory relay ordering")
        if off_event[2]:
            raise TestFailure(f"{target} OFF event must not claim a visible profile")
        if on_event[2] not in {"day", "daybreak", "night"}:
            raise TestFailure(f"{target} ON event has unknown profile {on_event[2]!r}")
        off_durations.append(on_event[0] - off_event[0])
        profiles.append(on_event[2])
        previous_timestamp = on_event[0]
    if profiles != ["day", "daybreak", "night", "day"]:
        raise TestFailure(f"{target} profile sequence is {profiles}")
    if off_durations[0] <= 5000 or off_durations[3] <= 5000:
        raise TestFailure(f"{target} DAY reset must remain OFF for more than 5 seconds")
    if any(duration > 5000 for duration in off_durations[1:3]):
        raise TestFailure(
            f"{target} DAY/DAYBREAK/NIGHT cycles must switch within 5 seconds"
        )


class _SelfTestState:
    def __init__(self) -> None:
        self.wifi_enabled = True
        self.override_until = 0.0
        self.override_targets: set[str] = set()
        self.command_results: dict[str, dict[str, Any]] = {}
        self.boot_slot = "ota_0"
        self.device_token = "0123456789abcdef0123456789abcdef"
        self.light_profiles = {"front": "day", "rear": "day"}


def _self_test_handler(state: _SelfTestState) -> type[http.server.BaseHTTPRequestHandler]:
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format_string: str, *args: object) -> None:
            del format_string, args

        def _json(self, status: int, payload: dict[str, Any]) -> None:
            encoded = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def _payload(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            content_type = self.headers.get("Content-Type", "")
            if content_type.startswith("application/x-www-form-urlencoded"):
                parsed = urllib.parse.parse_qs(raw.decode("utf-8"))
                decoded = {
                    key: values[-1] if values else "" for key, values in parsed.items()
                }
            else:
                decoded = json.loads(raw or b"{}")
            if not isinstance(decoded, dict):
                raise ValueError("payload must be an object")
            return decoded

        def do_GET(self) -> None:
            if not state.wifi_enabled:
                self._json(503, {"error": "wifi disabled"})
                return
            if self.path == "/api/health":
                self._json(200, {"healthy": True})
                return
            if self.path == "/api/status":
                active = time.monotonic() < state.override_until
                self._json(
                    200,
                    {
                        "controlState": {
                            "overrides": (
                                [
                                    {
                                        "target": target,
                                        "state": True,
                                        "remainingSec": 1,
                                    }
                                    for target in sorted(state.override_targets)
                                ]
                                if active
                                else []
                            )
                        },
                        "lights": {
                            target: {
                                "on": active
                                and relay_target in state.override_targets,
                                "profile": profile,
                                "known": True,
                                "transitioning": False,
                            }
                            for target, relay_target, profile in (
                                ("front", "light1", state.light_profiles["front"]),
                                ("rear", "light2", state.light_profiles["rear"]),
                            )
                        },
                    },
                )
                return
            if self.path == "/api/v2/capabilities":
                self._json(
                    200,
                    {
                        "type": "capabilities",
                        "v": 2,
                        "data": {
                            "features": {
                                "safeOta": True,
                                "aquaelLightProfiles": True,
                            },
                            "lightTargets": ["front", "rear"],
                            "limits": {
                                "lightProfileToggleMaxOffMs": 5000,
                                "lightResetThresholdMs": 5000,
                                "lightCycleOffMs": 1000,
                                "lightCalibrationOffMs": 6000,
                            },
                            "ota": {
                                "rollbackAvailable": True,
                                "updatePartitionBytes": 1_048_576,
                                "pendingVerify": False,
                                "state": "valid",
                            },
                        },
                    },
                )
                return
            if self.path == "/api/v2/ota/health":
                self._json(
                    200,
                    {
                        "healthy": True,
                        "bootSlot": state.boot_slot,
                        "rollbackAvailable": True,
                        "pendingValidation": False,
                    },
                )
                return
            self._json(404, {"error": "not found"})

        def do_POST(self) -> None:
            if self.path == "/lab/cut":
                state.wifi_enabled = False
                self._json(200, {"ok": True})
                return
            if self.path == "/lab/restore":
                state.wifi_enabled = True
                self._json(200, {"ok": True})
                return
            if not state.wifi_enabled:
                self._json(503, {"error": "wifi disabled"})
                return
            if self.path == "/api/v2/auth":
                payload = self._payload()
                if payload.get("pin") != "1234":
                    self._json(
                        401,
                        {
                            "type": "auth",
                            "v": 2,
                            "ok": False,
                            "code": "invalid_pin",
                        },
                    )
                    return
                self._json(
                    200,
                    {
                        "type": "auth",
                        "v": 2,
                        "ok": True,
                        "code": "authenticated",
                        "data": {
                            "sessionToken": state.device_token,
                            "expiresInSec": 300,
                        },
                    },
                )
                return
            if self.path in {"/api/action", "/api/v2/action"}:
                payload = self._payload()
                if payload.get("token") != state.device_token:
                    self._json(
                        401,
                        {
                            "type": "response",
                            "v": 2,
                            "ok": False,
                            "code": "session_required",
                        },
                    )
                    return
                command_id = str(payload["commandId"])
                if command_id in state.command_results:
                    result = dict(state.command_results[command_id])
                    result["duplicate"] = True
                    self._json(200, result)
                    return
                action = str(payload.get("action", ""))
                if action == "set_timed_override":
                    state.override_until = (
                        time.monotonic() + int(payload.get("durationSec", 1))
                    )
                    state.override_targets.add(str(payload.get("target", "")))
                    code = "override_active"
                elif action == "clear_timed_override":
                    state.override_targets.discard(str(payload.get("target", "")))
                    code = "automatic_restored"
                elif action == "set_light_profile":
                    target = str(payload.get("target", ""))
                    state.light_profiles[target] = str(payload.get("profile", ""))
                    code = "light_profile_transition_started"
                else:
                    self._json(
                        400,
                        {
                            "type": "response",
                            "v": 2,
                            "ok": False,
                            "code": "unknown_action",
                            "commandId": command_id,
                        },
                    )
                    return
                result = {
                    "commandId": command_id,
                    "type": "response",
                    "v": 2,
                    "ok": True,
                    "code": code,
                    "result": {
                        "target": payload.get("target"),
                        "state": payload.get("state"),
                    },
                }
                state.command_results[command_id] = result
                self._json(200, result)
                return
            if self.path == "/lab/aquael-trace":
                payload = self._payload()
                events: list[dict[str, Any]] = []
                for target, offset in (("front", 0), ("rear", 100)):
                    schedule = (
                        (0, 5501, "day"),
                        (6000, 11000, "daybreak"),
                        (11500, 16500, "night"),
                        (17000, 22501, "day"),
                    )
                    for index, (off_at, on_at, profile) in enumerate(schedule):
                        events.append(
                            {
                                "target": target,
                                "state": False,
                                "atMs": off_at + offset,
                                "profile": "",
                                "commandId": f"{target}-off-{index}",
                            }
                        )
                        events.append(
                            {
                                "target": target,
                                "state": True,
                                "atMs": on_at + offset,
                                "profile": profile,
                                "commandId": f"{target}-on-{index}",
                            }
                        )
                self._json(
                    200,
                    {
                        "runId": payload.get("runId"),
                        "events": events,
                        "conflicts": [],
                    },
                )
                return
            if self.path == "/api/v2/ota/rollback":
                state.boot_slot = "ota_1" if state.boot_slot == "ota_0" else "ota_0"
                self._json(202, {"accepted": True})
                return
            self._json(404, {"error": "not found"})

    return Handler


def run_self_test() -> int:
    state = _SelfTestState()
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), _self_test_handler(state)
    )
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base_url = f"http://127.0.0.1:{server.server_address[1]}"
    config = dataclasses.replace(
        Config.from_environment(),
        base_url=base_url,
        bearer_token="",
        admin_pin="1234",
        allow_mutations=True,
        wifi_cut_url=f"{base_url}/lab/cut",
        wifi_restore_url=f"{base_url}/lab/restore",
        wifi_outage_seconds=0.5,
        reconnect_timeout_seconds=3.0,
        override_ttl_seconds=1,
        override_grace_seconds=1.0,
        ota_health_path="/api/v2/ota/health",
        ota_rollback_url=f"{base_url}/api/v2/ota/rollback",
        allow_ota_rollback=True,
        aquael_trace_url=f"{base_url}/lab/aquael-trace",
        serial_port="",
    )
    try:
        suite = HilSuite(config)
        exit_code = suite.run()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2.0)
    expected_passes = 8
    pass_count = sum(result.outcome is Outcome.PASS for result in suite.results)
    skip_count = sum(result.outcome is Outcome.SKIP for result in suite.results)
    if exit_code or pass_count != expected_passes or skip_count != 1:
        print(
            f"Self-test contract failed: expected {expected_passes} PASS and 1 SKIP"
        )
        return 1
    print("HIL runner self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run AquaCYD hardware-in-the-loop resilience checks."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--dry-run",
        action="store_true",
        help="validate configuration without network, serial or state changes",
    )
    mode.add_argument(
        "--self-test",
        action="store_true",
        help="exercise the runner against an in-process simulated controller",
    )
    parser.add_argument(
        "--require-hardware",
        action="store_true",
        help="fail instead of skipping when AQUACYD_HIL_BASE_URL is absent",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            return run_self_test()
        config = Config.from_environment()
        if args.require_hardware and not config.base_url:
            raise ValueError(
                "AQUACYD_HIL_BASE_URL is required when --require-hardware is used"
            )
        return HilSuite(config, dry_run=args.dry_run).run()
    except ValueError as error:
        print(f"Configuration error: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
