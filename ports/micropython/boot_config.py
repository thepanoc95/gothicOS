"""Boot option parsing for the MicroPython osloader chain.

MicroPython-compatible subset of Python 3: no dataclasses, no f-strings, no pathlib.
Example: null.boot('-v -debug=0x01 -root=@flash/data -mem=200K')
"""

DEFAULT_INIT_PATH = "@flash/data/system/init.d.elf"
DEFAULT_SHELL_PATH = "@flash/data/user/bin/sh.uts"


def _parse_int(text, base):
    try:
        return int(text, base)
    except ValueError:
        return None


def _parse_memory(text):
    if not text:
        return None
    suffix = text[-1]
    multiplier = 1
    if suffix in ("K", "k"):
        multiplier = 1
        text = text[:-1]
    elif suffix in ("M", "m"):
        multiplier = 1024
        text = text[:-1]
    value = _parse_int(text, 10)
    if value is None:
        return None
    return value * multiplier


class BootOptions:
    def __init__(self):
        self.verbose = False
        self.debug_flags = 0
        self.root = ""
        self.memory_limit_kb = 0
        self.options = ""

    @classmethod
    def parse(cls, text):
        options = cls()
        options.options = text or ""
        for token in options.options.split():
            if token == "-v":
                options.verbose = True
            elif token.startswith("-debug="):
                value = token[7:]
                base = 16 if value[:2].lower() == "0x" else 10
                parsed = _parse_int(value, base)
                if parsed is None:
                    raise ValueError("invalid -debug value")
                options.debug_flags = parsed
            elif token.startswith("-root="):
                value = token[6:]
                if not value:
                    raise ValueError("empty -root value")
                options.root = value
            elif token.startswith("-mem="):
                parsed = _parse_memory(token[5:])
                if parsed is None:
                    raise ValueError("invalid -mem value")
                options.memory_limit_kb = parsed
            else:
                raise ValueError("unknown boot option: " + token)
        return options

    def _root(self):
        return self.root or "@flash/data"

    def init_path(self):
        return self._root().rstrip("/") + "/system/init.d.elf"

    def shell_path(self):
        return self._root().rstrip("/") + "/user/bin/sh.uts"

    @staticmethod
    def resolve(path):
        """Map the @flash VFS prefix onto the device filesystem."""
        if path.startswith("@flash/"):
            return path[len("@flash"):]
        if path == "@flash":
            return "/"
        return path

    def resolved_init_path(self):
        return self.resolve(self.init_path())

    def resolved_shell_path(self):
        return self.resolve(self.shell_path())

    def info(self):
        return {
            "verbose": self.verbose,
            "debug_flags": self.debug_flags,
            "root": self._root(),
            "memory_limit_kb": self.memory_limit_kb,
            "init_path": self.init_path(),
            "shell_path": self.shell_path(),
        }
