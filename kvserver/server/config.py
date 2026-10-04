"""
Configuration for the KV server.

Read from a TOML file (see kvserver.toml).  tomllib is part of the
standard library from Python 3.11 on, which is why the server asks for
that version.
"""
import tomllib

# Binary suffixes understood by parse_size().
_SIZE_UNITS = {
    "K": 1024,
    "M": 1024 ** 2,
    "G": 1024 ** 3,
    "T": 1024 ** 4,
}


def parse_size(value, default=0):
    """Turn "1G", "200M", "512" or an int into a number of bytes."""
    if value is None:
        return default
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)

    text = str(value).strip().upper()
    if not text:
        return default
    if text[-1] in _SIZE_UNITS:
        number, unit = text[:-1], text[-1]
    else:
        number, unit = text, ""
    try:
        amount = float(number)
    except ValueError:
        return default

    return int(amount * _SIZE_UNITS.get(unit, 1))


class Config:
    """The settings the server and the engine wrapper need."""

    def __init__(self, path):
        with open(path, "rb") as handle:
            data = tomllib.load(handle)

        kv = data.get("kvstore", {})
        http = data.get("http", {})

        self.host = kv.get("host", "0.0.0.0")
        self.port = int(kv.get("port", 8080))
        self.data_dir = kv.get("data_dir", "./data")
        self.table_name = kv.get("table_name", "kvstore")

        # A size of 0 means "leave the engine default alone".
        self.cache_size = parse_size(kv.get("cache_size"), 0)
        self.inmem_threshold = parse_size(kv.get("inmem_threshold"), 0)

        self.read_timeout = http.get("read_timeout", 30)
        self.write_timeout = http.get("write_timeout", 30)

    def __repr__(self):
        return (
            "Config(host=%r, port=%r, data_dir=%r, table_name=%r, "
            "cache_size=%r, inmem_threshold=%r)"
            % (
                self.host,
                self.port,
                self.data_dir,
                self.table_name,
                self.cache_size,
                self.inmem_threshold,
            )
        )
