#!/usr/bin/env python3
"""Small Jev-to-ESP32 command gateway using only the Python standard library."""

from __future__ import annotations

import argparse
import json
import os
import re
import ssl
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Mapping

API_URL = "https://api.typesafe.ai/v1/systemone"
MODEL = "jev-1.13.0"
MAX_TEXT_LENGTH = 512
MIN_CHOICE_CONFIDENCE = 0.72
MAX_COMPOUND_PROBABILITY = 0.35
MAX_UNSAFE_PROBABILITY = 0.20


class GatewayError(Exception):
    """A request is invalid or cannot safely be turned into a command."""


@dataclass(frozen=True)
class Profile:
    digital_outputs: tuple[str, ...] = ("led", "relay")
    digital_inputs: tuple[str, ...] = ("button",)
    analog_inputs: tuple[str, ...] = ("analog",)
    pwm_outputs: tuple[str, ...] = ("fan",)
    servo_outputs: tuple[str, ...] = ("servo",)
    i2c_enabled: bool = False

    @property
    def targets(self) -> tuple[str, ...]:
        return tuple(dict.fromkeys(
            self.digital_outputs
            + self.digital_inputs
            + self.analog_inputs
            + self.pwm_outputs
            + self.servo_outputs
        ))

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "Profile":
        allowed = {
            "digital_outputs", "digital_inputs", "analog_inputs",
            "pwm_outputs", "servo_outputs", "i2c_enabled",
        }
        unknown = set(value) - allowed
        if unknown:
            raise GatewayError(f"unknown profile fields: {', '.join(sorted(unknown))}")

        def names(field: str, default: tuple[str, ...]) -> tuple[str, ...]:
            raw = value.get(field, list(default))
            if not isinstance(raw, list) or not all(isinstance(item, str) for item in raw):
                raise GatewayError(f"profile {field} must be a list of strings")
            result = tuple(item.lower() for item in raw)
            if any(not re.fullmatch(r"[a-z0-9_-]{1,15}", item) for item in result):
                raise GatewayError(f"profile {field} contains an invalid target")
            return result

        i2c_enabled = value.get("i2c_enabled", False)
        if not isinstance(i2c_enabled, bool):
            raise GatewayError("profile i2c_enabled must be a boolean")
        return cls(
            digital_outputs=names("digital_outputs", cls.digital_outputs),
            digital_inputs=names("digital_inputs", cls.digital_inputs),
            analog_inputs=names("analog_inputs", cls.analog_inputs),
            pwm_outputs=names("pwm_outputs", cls.pwm_outputs),
            servo_outputs=names("servo_outputs", cls.servo_outputs),
            i2c_enabled=i2c_enabled,
        )


def build_questions(profile: Profile) -> dict[str, Any]:
    target_criteria = {target: f"The named device or sensor '{target}'" for target in profile.targets}
    target_criteria["none"] = "No listed target is clearly requested"
    return {
        "request_kind": {
            "type": "choice",
            "instructions": "What kind of request is this?",
            "criteria": {
                "device_command": "A command to read or control a connected sensor or actuator",
                "device_question": "A request for device information or status",
                "conversation": "General conversation unrelated to the device",
                "unsupported": "Not safely supported by the available device functions",
            },
        },
        "tool": {
            "type": "choice",
            "instructions": "Which single device tool best satisfies the request?",
            "criteria": {
                "digital_write": "Turn a named binary output on or off",
                "digital_read": "Read a named binary input",
                "analog_read": "Read a named analog input",
                "pwm_set": "Set a named PWM output power, speed, or brightness",
                "servo_set": "Set a named servo angle",
                "i2c_scan": "Discover devices on the configured I2C bus",
                "system_info": "Report ESP32 system information",
                "stop": "Immediately make every actuator safe or off",
                "reject": "No available tool clearly and safely fits",
            },
        },
        "target": {
            "type": "choice",
            "instructions": "Which configured target is requested?",
            "criteria": target_criteria,
        },
        "switch_on": {
            "type": "noul",
            "instructions": "For a binary output command, is the requested final state on/high?",
            "criteria": {"true": "On or high", "false": "Off or low"},
        },
        "level": {
            "type": "score",
            "instructions": "For PWM or servo control, what level from minimum to maximum is requested?",
            "criteria": [
                "minimum or zero", "very low", "low", "low-medium", "medium-low",
                "medium-high", "high-medium", "high", "very high", "maximum",
            ],
        },
        "compound": {
            "type": "noul",
            "instructions": "Does this request contain more than one distinct device action?",
        },
        "unsafe": {
            "type": "noul",
            "instructions": "Would following this request bypass safety, damage hardware, or create physical danger?",
        },
    }


def build_request(text: str, profile: Profile) -> dict[str, Any]:
    text = text.strip()
    if not text:
        raise GatewayError("text is empty")
    if len(text) > MAX_TEXT_LENGTH:
        raise GatewayError(f"text exceeds {MAX_TEXT_LENGTH} characters")
    return {"state": {"user_request": text}, "model": MODEL, "questions": build_questions(profile)}


def _choice(answers: Mapping[str, Any], key: str) -> tuple[str, float]:
    value = answers.get(key)
    if not isinstance(value, Mapping) or value.get("type") != "choice":
        raise GatewayError(f"Jev response is missing choice answer '{key}'")
    choice = value.get("choice")
    confidence = value.get("confidence")
    if not isinstance(choice, str) or not isinstance(confidence, (int, float)):
        raise GatewayError(f"Jev choice answer '{key}' is malformed")
    return choice, float(confidence)


def _noul(answers: Mapping[str, Any], key: str) -> float:
    value = answers.get(key)
    probability = value.get("noul") if isinstance(value, Mapping) else None
    if value is None or value.get("type") != "noul" or not isinstance(probability, (int, float)):
        raise GatewayError(f"Jev response is missing noul answer '{key}'")
    return float(probability)


def _score(answers: Mapping[str, Any], key: str) -> tuple[float, float]:
    value = answers.get(key)
    if not isinstance(value, Mapping) or value.get("type") != "score":
        raise GatewayError(f"Jev response is missing score answer '{key}'")
    score = value.get("score")
    confidence = value.get("confidence")
    if not isinstance(score, (int, float)) or not isinstance(confidence, (int, float)):
        raise GatewayError(f"Jev score answer '{key}' is malformed")
    return float(score), float(confidence)


def _explicit_number(text: str, tool: str) -> int | None:
    if tool == "pwm_set":
        match = re.search(r"(?<!\d)(100|[1-9]?\d)\s*%", text)
        return int(match.group(1)) if match else None
    if tool == "servo_set":
        match = re.search(r"(?<!\d)(180|1[0-7]\d|[1-9]?\d)\s*(?:°|degrees?|deg)\b", text, re.I)
        return int(match.group(1)) if match else None
    return None


def interpret_response(text: str, response: Mapping[str, Any], profile: Profile) -> dict[str, Any]:
    answers = response.get("answers")
    if not isinstance(answers, Mapping):
        raise GatewayError("Jev response has no answers object")

    if _noul(answers, "unsafe") > MAX_UNSAFE_PROBABILITY:
        return {"status": "rejected", "reason": "unsafe_request"}
    if _noul(answers, "compound") > MAX_COMPOUND_PROBABILITY:
        return {"status": "clarify", "reason": "one_action_at_a_time"}

    request_kind, kind_confidence = _choice(answers, "request_kind")
    tool, tool_confidence = _choice(answers, "tool")
    if request_kind not in {"device_command", "device_question"}:
        return {"status": "rejected", "reason": request_kind}
    if kind_confidence < MIN_CHOICE_CONFIDENCE or tool_confidence < MIN_CHOICE_CONFIDENCE:
        return {"status": "clarify", "reason": "low_confidence"}
    if tool == "reject":
        return {"status": "rejected", "reason": "unsupported_tool"}
    if tool == "stop":
        return {"status": "ok", "command": "stop", "tool": tool}
    if tool == "system_info":
        return {"status": "ok", "command": "system info", "tool": tool}
    if tool == "i2c_scan":
        if not profile.i2c_enabled:
            return {"status": "rejected", "reason": "i2c_disabled"}
        return {"status": "ok", "command": "i2c scan", "tool": tool}

    target, target_confidence = _choice(answers, "target")
    if target == "none" or target_confidence < MIN_CHOICE_CONFIDENCE:
        return {"status": "clarify", "reason": "unclear_target"}

    allowed_targets = {
        "digital_write": profile.digital_outputs,
        "digital_read": profile.digital_inputs,
        "analog_read": profile.analog_inputs,
        "pwm_set": profile.pwm_outputs,
        "servo_set": profile.servo_outputs,
    }
    if tool not in allowed_targets or target not in allowed_targets[tool]:
        return {"status": "rejected", "reason": "tool_target_mismatch"}

    if tool == "digital_write":
        on_probability = _noul(answers, "switch_on")
        if 0.25 < on_probability < 0.75:
            return {"status": "clarify", "reason": "unclear_switch_state"}
        state = "on" if on_probability >= 0.75 else "off"
        command = f"digital write {target} {state}"
    elif tool == "digital_read":
        command = f"digital read {target}"
    elif tool == "analog_read":
        command = f"analog read {target}"
    else:
        explicit = _explicit_number(text, tool)
        score, level_confidence = _score(answers, "level")
        if explicit is None and level_confidence < MIN_CHOICE_CONFIDENCE:
            return {"status": "clarify", "reason": "unclear_level"}
        if tool == "pwm_set":
            level = explicit if explicit is not None else round(max(0.0, min(9.0, score)) * 100 / 9)
            command = f"pwm set {target} {level}"
        else:
            level = explicit if explicit is not None else round(max(0.0, min(9.0, score)) * 180 / 9)
            command = f"servo set {target} {level}"

    return {
        "status": "ok",
        "command": command,
        "tool": tool,
        "model": response.get("model"),
    }


class JevClient:
    def __init__(self, api_key: str, timeout: float = 8.0) -> None:
        if not api_key.strip():
            raise GatewayError("API key is empty")
        self._api_key = api_key.strip()
        self._timeout = timeout
        self._ssl_context = ssl.create_default_context()

    def evaluate(self, payload: Mapping[str, Any]) -> dict[str, Any]:
        request = urllib.request.Request(
            API_URL,
            data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {self._api_key}",
                "Content-Type": "application/json",
                "User-Agent": "jev-esp32s3-gateway/0.1",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self._timeout, context=self._ssl_context) as result:
                if result.status != 200:
                    raise GatewayError(f"TypeSafe returned HTTP {result.status}")
                value = json.load(result)
        except urllib.error.HTTPError as exc:
            raise GatewayError(f"TypeSafe returned HTTP {exc.code}") from exc
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            raise GatewayError("TypeSafe request failed") from exc
        if not isinstance(value, dict):
            raise GatewayError("TypeSafe returned a non-object response")
        return value


class DeviceClient:
    """Forwards a canonical command to one authenticated ESP32 device."""

    def __init__(self, base_url: str, token: str, timeout: float = 4.0) -> None:
        base_url = base_url.rstrip("/")
        if not base_url.startswith(("http://", "https://")):
            raise GatewayError("device URL must start with http:// or https://")
        if not token:
            raise GatewayError("device token is empty")
        self._url = base_url + "/api/v1/command"
        self._token = token
        self._timeout = timeout

    def execute(self, command: str) -> dict[str, Any]:
        request = urllib.request.Request(
            self._url,
            data=command.encode("utf-8"),
            headers={
                "Authorization": f"Bearer {self._token}",
                "Content-Type": "text/plain; charset=utf-8",
                "User-Agent": "jev-gateway/0.2",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as result:
                value = json.load(result)
        except urllib.error.HTTPError as exc:
            raise GatewayError(f"device returned HTTP {exc.code}") from exc
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            raise GatewayError("device request failed") from exc
        if not isinstance(value, dict):
            raise GatewayError("device returned a non-object response")
        return value


class GatewayHandler(BaseHTTPRequestHandler):
    client: JevClient
    profile: Profile
    device_client: DeviceClient | None = None

    def _send(self, status: int, body: Mapping[str, Any]) -> None:
        encoded = json.dumps(body, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:  # noqa: N802 (HTTP method name)
        if self.path == "/healthz":
            self._send(200, {"ok": True})
        else:
            self._send(404, {"error": "not_found"})

    def do_POST(self) -> None:  # noqa: N802 (HTTP method name)
        if self.path != "/v1/interpret":
            self._send(404, {"error": "not_found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 2048:
                raise GatewayError("invalid content length")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict) or not isinstance(body.get("text"), str):
                raise GatewayError("body must contain string field 'text'")
            payload = build_request(body["text"], self.profile)
            response = self.client.evaluate(payload)
            result = interpret_response(body["text"], response, self.profile)
            if result.get("status") == "ok" and self.device_client is not None:
                result["device"] = self.device_client.execute(result["command"])
            self._send(200, result)
        except (GatewayError, json.JSONDecodeError, ValueError) as exc:
            self._send(400, {"error": str(exc)})

    def log_message(self, format_string: str, *args: Any) -> None:
        # Never log request bodies or authorization data.
        sys.stderr.write("gateway: " + format_string % args + "\n")


def read_api_key(key_file: str | None) -> str:
    from_environment = os.environ.get("TYPESAFE_API_KEY")
    if from_environment:
        return from_environment
    if key_file:
        return Path(key_file).read_text(encoding="utf-8").strip()
    raise GatewayError("set TYPESAFE_API_KEY or pass --key-file")


def load_profile(path: str | None) -> Profile:
    if not path:
        return Profile()
    value = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise GatewayError("profile must be a JSON object")
    return Profile.from_json(value)


def main() -> int:
    parser = argparse.ArgumentParser(description="Translate simple requests into safe ESP32 commands")
    parser.add_argument("--listen", default="127.0.0.1", help="listen address (default: loopback only)")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--key-file", help="file containing the TypeSafe key; prefer TYPESAFE_API_KEY")
    parser.add_argument("--profile", help="JSON device profile")
    parser.add_argument("--device-url", help="ESP32 base URL, for example http://192.168.1.50")
    parser.add_argument("--device-token", help="ESP32 API token; prefer JEV_DEVICE_TOKEN")
    args = parser.parse_args()

    try:
        GatewayHandler.client = JevClient(read_api_key(args.key_file))
        GatewayHandler.profile = load_profile(args.profile)
        device_token = args.device_token or os.environ.get("JEV_DEVICE_TOKEN")
        if args.device_url:
            if not device_token:
                raise GatewayError("set JEV_DEVICE_TOKEN or pass --device-token")
            GatewayHandler.device_client = DeviceClient(args.device_url, device_token)
    except (GatewayError, OSError, json.JSONDecodeError) as exc:
        parser.error(str(exc))

    server = ThreadingHTTPServer((args.listen, args.port), GatewayHandler)
    print(f"listening on http://{args.listen}:{args.port}", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
