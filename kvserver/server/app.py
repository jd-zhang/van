"""
Flask application exposing the KV store over HTTP.

Every answer is JSON with a "code" and a "code_name", the numeric codes
being the small ones the API publishes (0 / -1 / -2 / -3) rather than
the engine's own large negative values.
"""
import base64
import binascii

from flask import Flask, jsonify, request

from .kv_client import VAN_BAD, VAN_INVALID, VAN_NOTFOUND, VAN_OK

# Engine code -> published code.
JSON_CODE = {
    VAN_OK: 0,
    VAN_NOTFOUND: -1,
    VAN_INVALID: -2,
    VAN_BAD: -3,
}

CODE_NAME = {
    0: "VAN_OK",
    -1: "VAN_NOTFOUND",
    -2: "VAN_INVALID",
    -3: "VAN_BAD",
}

HTTP_STATUS = {
    0: 200,
    -1: 404,
    -2: 400,
    -3: 500,
}

DEFAULT_MESSAGE = {
    -1: "key not found",
    -2: "invalid argument",
    -3: "internal error",
}


def _ok(**extra):
    body = {"code": 0, "code_name": CODE_NAME[0]}
    body.update(extra)
    return jsonify(body), HTTP_STATUS[0]


def _error(json_code, message=None):
    return (
        jsonify(
            {
                "code": json_code,
                "code_name": CODE_NAME[json_code],
                "error": message or DEFAULT_MESSAGE[json_code],
            }
        ),
        HTTP_STATUS[json_code],
    )


def _invalid(message):
    return _error(-2, message)


def _decode_key(raw, encoding):
    """Turn a request parameter into the bytes used as the key."""
    if encoding == "base64":
        try:
            return base64.b64decode(raw, validate=True)
        except (binascii.Error, ValueError):
            return None
    return raw.encode("utf-8")


def create_app(db):
    app = Flask(__name__)

    #
    # The messages the API publishes are the fixed ones from the table
    # above; str(exc) would only add Python's own quoting and the raw
    # engine code around them.  The detail stays in the server log.
    #
    @app.errorhandler(KeyError)
    def _not_found(exc):
        app.logger.info("not found: %s", exc)
        return _error(-1)

    @app.errorhandler(ValueError)
    def _bad_argument(exc):
        app.logger.info("invalid argument: %s", exc)
        return _error(-2)

    @app.errorhandler(RuntimeError)
    def _internal(exc):
        app.logger.error("engine failure: %s", exc)
        return _error(-3)

    @app.errorhandler(404)
    def _unknown_path(exc):
        return _error(-1, "no such endpoint")

    @app.errorhandler(405)
    def _wrong_method(exc):
        return _error(-2, "method not allowed")

    @app.get("/api/v1/get")
    def api_get():
        raw = request.args.get("key")
        if raw is None:
            return _invalid("missing 'key' parameter")
        encoding = request.args.get("key_encoding", "text")
        if encoding not in ("text", "base64"):
            return _invalid("key_encoding must be 'text' or 'base64'")

        key = _decode_key(raw, encoding)
        if key is None:
            return _invalid("key is not valid base64")

        value = db.get(key)
        return _ok(data=base64.b64encode(value).decode("ascii"))

    @app.post("/api/v1/put")
    def api_put():
        body = request.get_json(silent=True)
        if not isinstance(body, dict):
            return _invalid("body must be a JSON object")

        raw = body.get("key")
        if raw is None:
            return _invalid("missing 'key'")
        if not isinstance(raw, str):
            return _invalid("'key' must be a string")

        encoding = body.get("key_encoding", "text")
        if encoding not in ("text", "base64"):
            return _invalid("key_encoding must be 'text' or 'base64'")

        key = _decode_key(raw, encoding)
        if key is None:
            return _invalid("key is not valid base64")

        encoded = body.get("value")
        if encoded is None:
            return _invalid("missing 'value'")
        if not isinstance(encoded, str):
            return _invalid("'value' must be a base64 string")
        try:
            value = base64.b64decode(encoded, validate=True)
        except (binascii.Error, ValueError):
            return _invalid("value is not valid base64")

        db.put(key, value)
        return _ok()

    @app.delete("/api/v1/delete")
    def api_delete():
        raw = request.args.get("key")
        if raw is None:
            return _invalid("missing 'key' parameter")
        encoding = request.args.get("key_encoding", "text")
        if encoding not in ("text", "base64"):
            return _invalid("key_encoding must be 'text' or 'base64'")

        key = _decode_key(raw, encoding)
        if key is None:
            return _invalid("key is not valid base64")

        # Deleting what is not there succeeds: DELETE is idempotent.
        db.delete(key)
        return _ok()

    return app
