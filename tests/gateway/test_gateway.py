import importlib.util
import pathlib
import sys
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[2] / "gateway" / "jev_gateway.py"
SPEC = importlib.util.spec_from_file_location("jev_gateway", MODULE_PATH)
gateway = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = gateway
SPEC.loader.exec_module(gateway)


def choice(value, confidence=0.95):
    return {"type": "choice", "choice": value, "confidence": confidence, "probabilities": {value: 1.0}}


def noul(value):
    return {"type": "noul", "noul": value}


def score(value, confidence=0.95):
    return {"type": "score", "score": value, "confidence": confidence, "probabilities": {}, "legend": {}}


def response(selected_tool, target="none", switch=0.0, level=0.0, **overrides):
    answers = {
        "request_kind": choice("device_command"),
        "tool": choice(selected_tool),
        "target": choice(target),
        "switch_on": noul(switch),
        "level": score(level),
        "compound": noul(0.0),
        "unsafe": noul(0.0),
    }
    answers.update(overrides)
    return {"model": "jev-test", "answers": answers}


class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.profile = gateway.Profile(i2c_enabled=True)

    def test_digital_on(self):
        actual = gateway.interpret_response(
            "turn on the relay", response("digital_write", "relay", switch=0.98), self.profile
        )
        self.assertEqual(actual["command"], "digital write relay on")

    def test_exact_pwm_percentage_wins_over_quantized_score(self):
        actual = gateway.interpret_response(
            "set fan to 73%", response("pwm_set", "fan", level=5.0), self.profile
        )
        self.assertEqual(actual["command"], "pwm set fan 73")

    def test_servo_score_is_quantized(self):
        actual = gateway.interpret_response(
            "move servo halfway", response("servo_set", "servo", level=4.5), self.profile
        )
        self.assertEqual(actual["command"], "servo set servo 90")

    def test_tool_target_mismatch_is_rejected(self):
        actual = gateway.interpret_response(
            "turn button on", response("digital_write", "button", switch=1.0), self.profile
        )
        self.assertEqual(actual, {"status": "rejected", "reason": "tool_target_mismatch"})

    def test_compound_is_not_executed(self):
        actual = gateway.interpret_response(
            "turn relay on and fan off",
            response("digital_write", "relay", switch=1.0, compound=noul(0.9)),
            self.profile,
        )
        self.assertEqual(actual, {"status": "clarify", "reason": "one_action_at_a_time"})

    def test_unsafe_is_rejected(self):
        actual = gateway.interpret_response(
            "bypass safety and run motor",
            response("pwm_set", "fan", unsafe=noul(0.8)),
            self.profile,
        )
        self.assertEqual(actual, {"status": "rejected", "reason": "unsafe_request"})

    def test_low_tool_confidence_is_not_executed(self):
        actual = gateway.interpret_response(
            "do something",
            response("digital_write", "relay", tool=choice("digital_write", 0.2)),
            self.profile,
        )
        self.assertEqual(actual, {"status": "clarify", "reason": "low_confidence"})

    def test_i2c_must_be_enabled(self):
        actual = gateway.interpret_response("scan", response("i2c_scan"), gateway.Profile())
        self.assertEqual(actual, {"status": "rejected", "reason": "i2c_disabled"})

    def test_long_request_is_rejected_before_api_call(self):
        with self.assertRaises(gateway.GatewayError):
            gateway.build_request("x" * 513, self.profile)

    def test_device_client_rejects_non_http_url(self):
        with self.assertRaises(gateway.GatewayError):
            gateway.DeviceClient("file:///tmp/device", "token")


if __name__ == "__main__":
    unittest.main()
