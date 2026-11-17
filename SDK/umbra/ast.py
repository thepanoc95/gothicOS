"""AST node definitions for UmbraTS."""

class Node:
    def __init__(self, kind, **fields):
        self.kind = kind
        for name, value in fields.items():
            setattr(self, name, value)

    def __repr__(self):
        fields = ", ".join(
            "%s=%r" % (name, getattr(self, name))
            for name in sorted(self.__dict__) if name != "kind"
        )
        return "%s(%s)" % (self.kind, fields)


def node(kind, **fields):
    return Node(kind, **fields)


BINARY_PRECEDENCE = {
    "||": 1,
    "&&": 2,
    "==": 3, "!=": 3,
    "<": 4, "<=": 4, ">": 4, ">=": 4,
    "+": 5, "-": 5,
    "*": 6, "/": 6, "%": 6,
}
