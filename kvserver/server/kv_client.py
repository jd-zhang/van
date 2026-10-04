"""
Python wrapper around the SWIG generated _kvstore module.

The extension is built next to this package (see ../Makefile), so the
startup script puts the kvserver directory on sys.path before importing
anything from here.
"""
import os

import kvstore

VAN_OK = kvstore.KV_OK
VAN_NOTFOUND = kvstore.KV_NOTFOUND
VAN_INVALID = kvstore.KV_INVALID
VAN_BAD = kvstore.KV_BAD

# What each engine return code means as a Python exception.
_EXCEPTIONS = {
    VAN_NOTFOUND: KeyError,
    VAN_INVALID: ValueError,
    VAN_BAD: RuntimeError,
}

_MESSAGES = {
    VAN_NOTFOUND: "key not found",
    VAN_INVALID: "invalid argument",
    VAN_BAD: "internal engine error",
}


def _check(ret):
    """Raise the Python exception that matches an engine return code."""
    if ret == VAN_OK:
        return
    kind = _EXCEPTIONS.get(ret, RuntimeError)
    raise kind("%s (code %d)" % (_MESSAGES.get(ret, "unknown error"), ret))


def _as_bytes(name, value):
    if isinstance(value, bytes):
        return value
    if isinstance(value, bytearray) or isinstance(value, memoryview):
        return bytes(value)
    if isinstance(value, str):
        # Convenience only: the server always hands us bytes.
        return value.encode("utf-8")
    raise TypeError("%s must be bytes, not %s" % (name, type(value).__name__))


class KVStore:
    """The one database the server talks to.

    Use it as a context manager so that the engine is closed even when
    the server is interrupted.
    """

    def __init__(self, config):
        self.config = config
        self.opened = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def open(self):
        if self.opened:
            return
        # The engine expects the directory to be there already and fails
        # the open when it is not, so make it before handing it over.
        os.makedirs(self.config.data_dir, exist_ok=True)
        ret = kvstore.kv_open(
            self.config.data_dir,
            self.config.table_name,
            self.config.cache_size,
            self.config.inmem_threshold,
        )
        _check(ret)
        self.opened = True

    def close(self):
        if not self.opened:
            return
        self.opened = False
        kvstore.kv_close()

    def get(self, key):
        """Return the value of 'key' as bytes, or raise KeyError."""
        key = _as_bytes("key", key)
        ret, value = kvstore.kv_get(key)
        _check(ret)
        if value is None:
            raise KeyError("key not found")
        return value

    def put(self, key, value):
        key = _as_bytes("key", key)
        value = _as_bytes("value", value)
        _check(kvstore.kv_put(key, value))

    def delete(self, key):
        """Delete 'key'.  Removing what is not there is not an error."""
        key = _as_bytes("key", key)
        _check(kvstore.kv_del(key))
