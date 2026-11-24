import base64
import hashlib
import importlib.util
import io
import json
import os
import re
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from unittest.mock import patch

import jwt
from cryptography.hazmat.primitives.asymmetric import rsa

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "tdeck_connect", ROOT / "bin/connect-chatgpt.py"
)
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)


class OAuthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    def token(self, **changes):
        claims = dict(
            iss=app.ISSUER,
            aud="oaiapp_unit",
            sub="unit-user",
            exp=int(time.time()) + 300,
            nonce="expected",
        )
        claims.update(changes)
        return jwt.encode(claims, self.key, algorithm="RS256")

    def test_identity_checks(self):
        public = self.key.public_key()
        self.assertEqual(
            app.verify_identity(self.token(), public, "oaiapp_unit", "expected")["sub"],
            "unit-user",
        )
        with self.assertRaises(app.SetupError):
            app.verify_identity(self.token(), public, "oaiapp_unit", "wrong")
        with self.assertRaises(jwt.InvalidAudienceError):
            app.verify_identity(self.token(), public, "oaiapp_other", "expected")
        with self.assertRaises(jwt.ExpiredSignatureError):
            app.verify_identity(
                self.token(exp=int(time.time()) - 100),
                public,
                "oaiapp_unit",
                "expected",
            )
        other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        with self.assertRaises(jwt.InvalidSignatureError):
            app.verify_identity(
                self.token(), other.public_key(), "oaiapp_unit", "expected"
            )

    def test_fragmented_usb_host_id(self):
        response = b"@@ok oauth connected=0 import=0 host=urn:uuid:11111111-2222-4333-8444-555555555555\n"
        fragments = iter(bytes([value]) for value in response)
        port = app.Port.__new__(app.Port)
        port.fd = 123
        pattern = rb"@@ok oauth connected=(\d) import=(-?\d+) host=(urn:uuid:[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})"
        with patch.object(app.termios, "tcflush"), patch.object(
            app.os, "write"
        ), patch.object(
            app.select, "select", return_value=([123], [], [])
        ), patch.object(
            app.os, "read", side_effect=lambda *args: next(fragments)
        ):
            result = port.command("@@oauthinfo", pattern)
        self.assertEqual(
            result.group(3), b"urn:uuid:11111111-2222-4333-8444-555555555555"
        )

    def test_timestamp(self):
        self.assertEqual(
            app.epoch("2026-11-24T21:00:00Z"), app.epoch("2026-11-24T22:00:00+01:00")
        )
        self.assertEqual(app.epoch(None), 0)
        with self.assertRaises(app.SetupError):
            app.epoch("2026-11-24T21:00:00")

    def test_atomic_private_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "private" / "registration.json"
            app.save_private(path, {"client_id": "oaiapp_test"})
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(path.parent.stat().st_mode & 0o777, 0o700)
            self.assertEqual(json.loads(path.read_text())["client_id"], "oaiapp_test")

    def test_transfer_integrity_and_no_secret_echo(self):
        credentials = {
            "fake": "fake-only" * 250,
            "ext_agent_host_id": "urn:uuid:11111111-2222-4333-8444-555555555555",
        }
        commands = []

        class FakePort:
            def command(self, text, pattern):
                commands.append(text)
                self_outer.assertLessEqual(len(text.removeprefix("@@")), 62)
                if text == "@@oauthinfo":
                    return re.search(
                        pattern,
                        b"@@ok oauth connected=1 import=3 host=urn:uuid:11111111-2222-4333-8444-555555555555",
                    )

        self_outer = self
        app.transfer(FakePort(), credentials)
        data = b"".join(
            base64.b64decode(c.removeprefix("@@oauthadd "))
            for c in commands
            if c.startswith("@@oauthadd ")
        )
        end = next(c for c in commands if c.startswith("@@oauthend ")).split()
        self.assertEqual(int(end[1]), len(data))
        self.assertEqual(
            end[2],
            base64.urlsafe_b64encode(hashlib.sha256(data).digest())
            .rstrip(b"=")
            .decode(),
        )
        self.assertEqual(json.loads(data), credentials)

    def test_callback_state_pkce_and_grant(self):
        captured = {}
        failures = []

        class Output(io.StringIO):
            def write(self, value):
                result = super().write(value)
                if value.startswith(app.ISSUER + "/api/accounts/authorize?"):
                    captured.update(
                        urllib.parse.parse_qs(urllib.parse.urlsplit(value).query)
                    )
                    threading.Thread(target=callback, daemon=True).start()
                return result

        def callback():
            try:
                redirect = captured["redirect_uri"][0]
                bad = (
                    redirect
                    + "?"
                    + urllib.parse.urlencode(
                        {"state": "wrong", "code": "fake", "client_id": "oaiapp_unit"}
                    )
                )
                try:
                    urllib.request.urlopen(bad, timeout=3)
                    failures.append("wrong state accepted")
                except urllib.error.HTTPError as error:
                    if error.code != 400:
                        failures.append("wrong rejection status")
                good = (
                    redirect
                    + "?"
                    + urllib.parse.urlencode(
                        {
                            "state": captured["state"][0],
                            "code": "fake-code",
                            "client_id": "oaiapp_unit",
                        }
                    )
                )
                with urllib.request.urlopen(good, timeout=3) as result:
                    result.read()
            except Exception:
                failures.append("callback failed")

        def fake_api(url, data=None, bearer=None, maximum=65536):
            if url.endswith("/token"):
                self.assertEqual(data["client_id"], "oaiapp_unit")
                self.assertEqual(data["redirect_uri"], captured["redirect_uri"][0])
                digest = (
                    base64.urlsafe_b64encode(
                        hashlib.sha256(data["code_verifier"].encode()).digest()
                    )
                    .rstrip(b"=")
                    .decode()
                )
                self.assertEqual(digest, captured["code_challenge"][0])
                return dict(
                    id_token=self.token(nonce=captured["nonce"][0]),
                    access_token="fake-access",
                    refresh_token="fake-refresh",
                    scope=app.SCOPES,
                    token_type="Bearer",
                    expires_in=3600,
                )
            self.assertEqual(bearer, "fake-access")
            return {"models": [{"slug": "gpt-unit", "visibility": "list"}]}

        class FakeKeys:
            def __init__(self, *args, **kwargs):
                pass

            def get_signing_key_from_jwt(self, token):
                return type("Key", (), {"key": self_outer.key.public_key()})()

        self_outer = self
        output = Output()
        metadata = {
            "authorization_endpoint": app.ISSUER + "/api/accounts/authorize",
            "token_endpoint": app.ISSUER + "/api/accounts/oauth/token",
            "jwks_uri": app.ISSUER + "/.well-known/jwks.json",
        }
        with tempfile.TemporaryDirectory() as tmp, patch.object(
            app, "api_json", fake_api
        ), patch.object(app.jwt, "PyJWKClient", FakeKeys), patch("sys.stdout", output):
            result = app.authorize(
                metadata,
                {},
                "urn:uuid:11111111-2222-4333-8444-555555555555",
                Path(tmp) / "registration.json",
            )
            saved = json.loads((Path(tmp) / "registration.json").read_text())
        self.assertFalse(failures)
        self.assertEqual(result["model"], "gpt-unit")
        self.assertNotIn("refresh_token", saved)
        self.assertNotIn("fake-access", output.getvalue())
        self.assertNotIn("fake-refresh", output.getvalue())


if __name__ == "__main__":
    unittest.main()
