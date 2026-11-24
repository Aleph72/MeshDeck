#!/usr/bin/env python3
"""Connect a T-Deck Field to the user's ChatGPT plan through official OAuth."""

import argparse
import base64
import hashlib
import hmac
import http.server
import json
import os
import re
import secrets
import select
import ssl
import sys
import termios
import threading
import time
import tty
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

import certifi
import jwt

ISSUER = "https://auth.openai.com"
RESOURCE = "https://api.openai.com/v1"
SCOPES = "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"


class SetupError(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, target):
        return None


def api_json(url, data=None, bearer=None, maximum=65536):
    if not (
        url.startswith(ISSUER + "/")
        or url.startswith(RESOURCE + "/")
        or url == RESOURCE + "/models"
    ):
        raise SetupError("Endpoint non consentito")
    headers = {
        "Accept": "application/json",
        "User-Agent": "TDeck-Field/1.0 ChatGPT-sign-in",
    }
    if data is not None:
        data = urllib.parse.urlencode(data).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if bearer:
        headers["Authorization"] = "Bearer " + bearer
    request = urllib.request.Request(url, data=data, headers=headers)
    opener = urllib.request.build_opener(
        NoRedirect(),
        urllib.request.HTTPSHandler(
            context=ssl.create_default_context(cafile=certifi.where())
        ),
    )
    try:
        with opener.open(request, timeout=30) as response:
            raw = response.read(maximum + 1)
            if len(raw) > maximum:
                raise SetupError(
                    "Risposta oltre il limite previsto per "
                    + urllib.parse.urlsplit(url).path
                )
            return json.loads(raw)
    except urllib.error.HTTPError as error:
        raise SetupError(
            "Il servizio di accesso ha risposto HTTP " + str(error.code)
        ) from None


def epoch(value):
    if value is None:
        return 0
    if isinstance(value, (int, float)) or (
        isinstance(value, str) and value.isdecimal()
    ):
        result = int(value)
    else:
        parsed = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            raise SetupError("Scadenza senza fuso orario")
        result = int(parsed.timestamp())
    if not 0 <= result <= 4294967295:
        raise SetupError("Scadenza non valida")
    return result


def save_private(path, value):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(path.parent, 0o700)
    temporary = path.with_name(path.name + "." + secrets.token_hex(6) + ".tmp")
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "w") as out:
            json.dump(value, out, separators=(",", ":"))
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.old = termios.tcgetattr(self.fd)
        tty.setraw(self.fd, termios.TCSANOW)

    def close(self):
        termios.tcsetattr(self.fd, termios.TCSANOW, self.old)
        os.close(self.fd)

    def command(self, command, pattern, timeout=6):
        if len(command.removeprefix("@@")) > 62:
            raise SetupError("Comando USB troppo lungo")
        termios.tcflush(self.fd, termios.TCIFLUSH)
        os.write(self.fd, command.encode("ascii") + b"\n")
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            readable, _, _ = select.select([self.fd], [], [], 0.1)
            if readable:
                buf.extend(os.read(self.fd, 32768))
                found = re.search(pattern, buf)
                if found:
                    return found
                if len(buf) > 65536:
                    del buf[:-8192]
        raise SetupError("Il T-Deck non ha confermato il comando USB")


def verify_identity(token, signing_key, client_id, nonce):
    claims = jwt.decode(
        token,
        signing_key,
        algorithms=["RS256"],
        audience=client_id,
        issuer=ISSUER,
        options={"require": ["iss", "aud", "sub", "exp", "nonce"]},
        leeway=5,
    )
    if not isinstance(claims.get("nonce"), str) or not hmac.compare_digest(
        claims["nonce"], nonce
    ):
        raise SetupError("Identita' non verificata: nonce errato")
    return claims


def authorize(metadata, registration, host_id, profile_file, pending_file=None):
    state, nonce, verifier = (secrets.token_urlsafe(48) for _ in range(3))
    challenge = (
        base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest())
        .rstrip(b"=")
        .decode()
    )
    callback = {}
    received = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.headers.get("Host") != "127.0.0.1:" + str(self.server.server_port):
                self.send_error(400)
                return
            parsed = urllib.parse.urlsplit(self.path)
            if parsed.path != "/auth/callback" or len(self.path) > 16384:
                self.send_error(404)
                return
            try:
                params = urllib.parse.parse_qs(parsed.query, max_num_fields=16)
                if any(len(values) != 1 for values in params.values()):
                    raise ValueError()
                supplied = params.get("state", [""])[0]
                if not hmac.compare_digest(supplied, state) or received.is_set():
                    raise ValueError()
                callback.update({k: values[0] for k, values in params.items()})
            except (ValueError, TypeError):
                self.send_error(400)
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Security-Policy", "default-src 'none'")
            self.end_headers()
            self.wfile.write(
                b"<!doctype html><title>T-Deck Field</title><p>Puoi tornare a Codex. Il collegamento viene verificato.</p>"
            )
            received.set()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    redirect = "http://127.0.0.1:" + str(server.server_port) + "/auth/callback"
    params = {
        "client_id": registration.get("client_id", "dynamic_agent_client"),
        "ext_agent_host_id": host_id,
        "response_type": "code",
        "redirect_uri": redirect,
        "scope": SCOPES,
        "resource": RESOURCE,
        "state": state,
        "nonce": nonce,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
    }
    if params["client_id"] == "dynamic_agent_client":
        params["agent_name_hint"] = "T-Deck Field"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print(
        "Apri questo collegamento NEL BROWSER DEL MAC e autorizza T-Deck Field a usare il tuo piano ChatGPT:",
        flush=True,
    )
    print(
        metadata["authorization_endpoint"] + "?" + urllib.parse.urlencode(params),
        flush=True,
    )
    try:
        if not received.wait(600):
            raise SetupError(
                "Tempo per l'accesso terminato; nessuna sessione importata"
            )
    finally:
        server.shutdown()
        server.server_close()
    if callback.get("error"):
        raise SetupError(
            "Accesso o autorizzazione rifiutati; nessuna sessione importata"
        )
    issued = callback.get("client_id", registration.get("client_id", ""))
    if not re.fullmatch(r"oaiapp_[A-Za-z0-9_-]{1,120}", issued) or not callback.get(
        "code"
    ):
        raise SetupError("Registrazione incompleta")
    if registration.get("client_id") and issued != registration["client_id"]:
        raise SetupError(
            "La registrazione restituita non corrisponde all'account selezionato"
        )
    registration["client_id"] = issued
    registration["ext_agent_host_id"] = host_id
    save_private(profile_file, registration)
    tokens = api_json(
        metadata["token_endpoint"],
        {
            "grant_type": "authorization_code",
            "client_id": issued,
            "code": callback["code"],
            "code_verifier": verifier,
            "redirect_uri": redirect,
            "resource": RESOURCE,
        },
        maximum=131072,
    )
    if not isinstance(tokens.get("id_token"), str):
        raise SetupError("Il servizio non ha restituito un'identita' verificabile")
    key = jwt.PyJWKClient(
        metadata["jwks_uri"],
        ssl_context=ssl.create_default_context(cafile=certifi.where()),
    ).get_signing_key_from_jwt(tokens["id_token"])
    identity = verify_identity(tokens["id_token"], key.key, issued, nonce)
    if registration.get("subject") and registration["subject"] != identity["sub"]:
        raise SetupError("Account diverso da quello associato alla registrazione")
    if (
        "chatgpt.tokens.use.direct" not in tokens.get("scope", "").split()
        or "resource.invoke" not in tokens.get("scope", "").split()
    ):
        raise SetupError(
            "Uso del piano ChatGPT non autorizzato; nessuna richiesta di inferenza inviata"
        )
    if tokens.get("token_type", "").lower() != "bearer" or not tokens.get(
        "refresh_token"
    ):
        raise SetupError("Sessione non rinnovabile; completa di nuovo l'accesso")
    registration.update(
        subject=identity["sub"], issuer=identity["iss"], email=identity.get("email", "")
    )
    save_private(profile_file, registration)
    credentials = {
        "issuer": identity["iss"],
        "subject": identity["sub"],
        "email": identity.get("email", ""),
        "client_id": issued,
        "ext_agent_host_id": host_id,
        "access_token": tokens["access_token"],
        "refresh_token": tokens["refresh_token"],
        "id_token": tokens["id_token"],
        "scope": tokens["scope"],
        "expires_at": int(time.time()) + int(tokens["expires_in"]),
        "earliest_refresh_at": epoch(tokens.get("earliest_refresh_at", 0)),
        "model": "",
    }
    if pending_file is not None:
        save_private(pending_file, credentials)
    choose_model(credentials)
    return credentials


def choose_model(credentials):
    catalog = api_json(
        RESOURCE + "/models",
        bearer=credentials["access_token"],
        maximum=4 * 1024 * 1024,
    )
    models = [
        item
        for item in catalog.get("models", [])
        if item.get("visibility") == "list"
        and re.fullmatch(r"[A-Za-z0-9_.-]{1,95}", item.get("slug", ""))
    ]
    if not models:
        raise SetupError("Nessun modello disponibile per l'account autorizzato")
    credentials["model"] = models[0]["slug"]


def transfer(port, credentials):
    payload = json.dumps(
        credentials, separators=(",", ":"), ensure_ascii=False
    ).encode()
    if len(payload) > 12288:
        raise SetupError("La sessione supera lo spazio previsto sul dispositivo")
    port.command("@@oauthbegin", rb"@@ok oauth begin ok")
    for offset in range(0, len(payload), 30):
        chunk = base64.b64encode(payload[offset : offset + 30]).decode()
        port.command("@@oauthadd " + chunk, rb"@@ok oauth chunk ok")
    digest = (
        base64.urlsafe_b64encode(hashlib.sha256(payload).digest()).rstrip(b"=").decode()
    )
    port.command(
        "@@oauthend " + str(len(payload)) + " " + digest, rb"@@ok oauth committing"
    )
    for _ in range(10):
        status = port.command(
            "@@oauthinfo",
            rb"@@ok oauth connected=(\d) import=(-?\d+) host=(urn:uuid:[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})",
        )
        if status.group(1) == b"1" and status.group(2) == b"3":
            return
        if status.group(2) == b"-1":
            raise SetupError("Il T-Deck non ha potuto salvare la sessione")
        time.sleep(0.5)
    raise SetupError("Salvataggio non confermato")


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem2201")
    parser.add_argument(
        "--state-dir", type=Path, default=Path.home() / ".config/tdeck-field"
    )
    parser.add_argument("--new-account", action="store_true")
    parser.add_argument(
        "--profile", default="default", help="Saved account registration to use"
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        help="Transfer a protected pending session without signing in again",
    )
    args = parser.parse_args()
    port = Port(args.port)
    try:
        info = port.command(
            "@@oauthinfo",
            rb"@@ok oauth connected=(\d) import=(-?\d+) host=(urn:uuid:[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})",
        )
        host_id = info.group(3).decode()
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.profile):
            raise SetupError("Nome del profilo non valido")
        profile_name = (
            "account-" + secrets.token_hex(8) if args.new_account else args.profile
        )
        folder = args.state_dir / host_id.removeprefix("urn:uuid:") / profile_name
        folder.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(folder, 0o700)
        profile = folder / "registration.json"
        pending = folder / "pending-session.json"
        registration = (
            json.loads(profile.read_text())
            if profile.exists() and not args.new_account
            else {}
        )
        if pending.exists() and not args.resume:
            raise SetupError(
                "Esiste una sessione protetta in attesa; riprendi con --resume"
            )
        if args.resume:
            credentials = json.loads(pending.read_text())
            if credentials.get("ext_agent_host_id") != host_id:
                raise SetupError(
                    "La sessione in attesa appartiene a un altro dispositivo"
                )
            if not credentials.get("model"):
                choose_model(credentials)
                save_private(pending, credentials)
        else:
            metadata = api_json(ISSUER + "/.well-known/openid-configuration")
            expected = {
                "issuer": ISSUER,
                "authorization_endpoint": ISSUER + "/api/accounts/authorize",
                "token_endpoint": ISSUER + "/api/accounts/oauth/token",
                "jwks_uri": ISSUER + "/.well-known/jwks.json",
            }
            if any(metadata.get(k) != value for k, value in expected.items()):
                raise SetupError("Metadati di accesso non riconosciuti")
            credentials = authorize(metadata, registration, host_id, profile, pending)
            save_private(pending, credentials)
        print(
            "Identita' e permesso di usare il piano verificati. Trasferimento USB in corso; token non mostrati.",
            flush=True,
        )
        transfer(port, credentials)
        pending.unlink(missing_ok=True)
        print(
            "Account collegato al T-Deck. Ora puo' usare il tuo piano via Wi-Fi senza il Mac. Nessuna inferenza inviata durante il collegamento.",
            flush=True,
        )
    finally:
        port.close()


if __name__ == "__main__":
    try:
        main()
    except SetupError as error:
        print(
            "Collegamento non completato: "
            + str(error)
            + ". Credenziali non mostrate.",
            file=sys.stderr,
        )
        raise SystemExit(1)
    except (OSError, ValueError, TypeError, jwt.PyJWTError, urllib.error.URLError):
        print(
            "Collegamento non completato. Credenziali non mostrate; l'eventuale sessione in attesa e' protetta sul Mac.",
            file=sys.stderr,
        )
        raise SystemExit(1)
