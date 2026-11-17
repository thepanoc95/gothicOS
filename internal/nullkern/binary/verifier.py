"""Verifier for Umbra Bytecode containers.

The verifier is intentionally deterministic and allocation-light so it can run on the
RP2 port. It validates structure, section bounds, opcodes, operand ranges, control flow,
stack depth, function references, native calls, ABI compatibility, and resource limits.
"""

try:
    import ustruct as struct
except ImportError:
    import struct

try:
    import json
except ImportError:
    json = None

from . import format as fmt

VALID_OPCODES = (
    fmt.OP_NOP, fmt.OP_PUSH_CONST, fmt.OP_PUSH_TRUE, fmt.OP_PUSH_FALSE,
    fmt.OP_PUSH_NULL, fmt.OP_POP, fmt.OP_DUP, fmt.OP_LOAD_LOCAL,
    fmt.OP_STORE_LOCAL, fmt.OP_ADD, fmt.OP_SUB, fmt.OP_MUL, fmt.OP_DIV,
    fmt.OP_MOD, fmt.OP_NEG, fmt.OP_NOT, fmt.OP_EQ, fmt.OP_NE, fmt.OP_LT,
    fmt.OP_LE, fmt.OP_GT, fmt.OP_GE, fmt.OP_JMP, fmt.OP_JZ, fmt.OP_JNZ,
    fmt.OP_CALL, fmt.OP_CALL_NATIVE, fmt.OP_RET, fmt.OP_RET_VOID,
    fmt.OP_HALT, fmt.OP_ARRAY_NEW, fmt.OP_ARRAY_GET, fmt.OP_ARRAY_SET,
    fmt.OP_YIELD,
)


class VerificationResult:
    __slots__ = ("ok", "errors", "metadata", "header", "image")

    def __init__(self, ok, errors, metadata=None, header=None, image=None):
        self.ok = ok
        self.errors = errors
        self.metadata = metadata
        self.header = header
        self.image = image


class _Verifier:
    def __init__(self, package, limits):
        self.package = package
        self.limits = limits or {}
        self.errors = []
        self.header = None
        self.functions = []
        self.constants = []
        self.metadata = None

    def fail(self, message):
        self.errors.append(message)

    def verify(self):
        self.verify_header()
        if self.errors:
            return VerificationResult(False, self.errors)
        self.verify_constants()
        self.verify_functions()
        self.verify_code()
        self.verify_metadata()
        image = None
        if not self.errors:
            image = {
                "constants": self.constants,
                "functions": self.functions,
                "code_offset": self.code_offset,
                "metadata_offset": self.metadata_offset,
                "package": self.package,
            }
        return VerificationResult(
            not self.errors, self.errors, self.metadata, self.header, image
        )

    def verify_header(self):
        package = self.package
        if not isinstance(package, (bytes, bytearray)):
            self.fail("package must be bytes")
            return
        if len(package) < fmt.HEADER_SIZE:
            self.fail("package is shorter than the header")
            return
        fields = struct.unpack_from("<4sBBBBHHHHHHHH", package, 0)
        self.header = {
            "magic": fields[0],
            "version": (fields[1], fields[2]),
            "abi": (fields[3], fields[4]),
            "flags": fields[5],
            "entry": fields[6],
            "function_count": fields[7],
            "constant_count": fields[8],
            "constants_size": fields[9],
            "code_size": fields[10],
            "metadata_size": fields[11],
            "reserved": fields[12],
        }
        header = self.header
        if header["magic"] != fmt.MAGIC:
            self.fail("invalid Umbra magic")
        if header["version"] != (fmt.VERSION_MAJOR, fmt.VERSION_MINOR):
            self.fail("unsupported bytecode version")
        if (header["abi"][0], header["abi"][1]) != (fmt.ABI_MAJOR, fmt.ABI_MINOR):
            self.fail("kernel ABI is incompatible with this bytecode")
        if header["flags"] != 0 or header["reserved"] != 0:
            self.fail("reserved header fields must be zero")
        if header["function_count"] < 1 or header["function_count"] > fmt.MAX_FUNCTIONS:
            self.fail("invalid function count")
        if header["constant_count"] > fmt.MAX_CONSTANTS:
            self.fail("invalid constant count")
        if header["code_size"] < 2 or header["code_size"] > fmt.MAX_CODE_SIZE:
            self.fail("invalid code size")
        if header["metadata_size"] < 2:
            self.fail("invalid metadata size")
        table_size = header["function_count"] * fmt.FUNCTION_RECORD_SIZE
        self.function_offset = fmt.HEADER_SIZE
        self.constant_offset = self.function_offset + table_size
        self.code_offset = self.constant_offset + header["constants_size"]
        self.metadata_offset = self.code_offset + header["code_size"]
        expected = self.metadata_offset + header["metadata_size"]
        if expected != len(package):
            self.fail("section sizes do not match package length")
        if header["entry"] >= header["function_count"]:
            self.fail("entry function index is out of range")

    def verify_constants(self):
        package = self.package
        offset = self.constant_offset
        end = self.code_offset
        for index in range(self.header["constant_count"]):
            if offset >= end:
                self.fail("constants section ended early")
                return
            tag = package[offset]
            offset += 1
            if tag == fmt.CONST_NULL:
                value = None
            elif tag == fmt.CONST_BOOL:
                if offset + 1 > end:
                    self.fail("truncated boolean constant")
                    return
                value = package[offset] != 0
                offset += 1
            elif tag == fmt.CONST_INT:
                if offset + 4 > end:
                    self.fail("truncated integer constant")
                    return
                value = struct.unpack_from("<i", package, offset)[0]
                offset += 4
            elif tag == fmt.CONST_FLOAT:
                if offset + 8 > end:
                    self.fail("truncated float constant")
                    return
                value = struct.unpack_from("<d", package, offset)[0]
                offset += 8
            elif tag == fmt.CONST_STRING:
                if offset + 2 > end:
                    self.fail("truncated string constant")
                    return
                length = struct.unpack_from("<H", package, offset)[0]
                offset += 2
                if offset + length > end:
                    self.fail("truncated string contents")
                    return
                value = bytes(package[offset:offset + length]).decode("utf-8")
                offset += length
            else:
                self.fail("unknown constant tag")
                return
            self.constants.append(value)
        if offset != end:
            self.fail("constants section has trailing bytes")

    def verify_functions(self):
        package = self.package
        count = self.header["function_count"]
        for index in range(count):
            offset = self.function_offset + index * fmt.FUNCTION_RECORD_SIZE
            fields = struct.unpack_from("<BBHHBB", package, offset)
            nlocals, nparams, code_offset, code_size, flags, reserved = fields
            if nlocals > fmt.MAX_LOCALS:
                self.fail("invalid local count in function %d" % index)
            if nparams > nlocals:
                self.fail("parameter count exceeds locals in function %d" % index)
            if reserved != 0 or flags not in (0, 1):
                self.fail("invalid function flags in function %d" % index)
            if code_size < 2 or code_size % fmt.INSTRUCTION_SIZE != 0:
                self.fail("invalid code size in function %d" % index)
            if code_offset < 0 or code_offset + code_size > self.header["code_size"]:
                self.fail("code bounds invalid in function %d" % index)
            self.functions.append({
                "nlocals": nlocals,
                "nparams": nparams,
                "code_offset": code_offset,
                "code_size": code_size,
                "flags": flags,
            })
        entry = self.header["entry"]
        if entry < len(self.functions) and not (self.functions[entry]["flags"] & 1):
            self.fail("entry function is not marked as an entry point")

    def verify_code(self):
        package = self.package
        max_stack = self.limits.get("max_stack", fmt.MAX_STACK_DEPTH)
        for index, function in enumerate(self.functions):
            start = self.code_offset + function["code_offset"]
            count = function["code_size"] // fmt.INSTRUCTION_SIZE
            instructions = []
            for position in range(count):
                offset = start + position * fmt.INSTRUCTION_SIZE
                opcode, operand = struct.unpack_from("<BB", package, offset)
                instructions.append((opcode, operand))
                if opcode not in VALID_OPCODES:
                    self.fail("invalid opcode %d in function %d" % (opcode, index))
                    continue
                if opcode == fmt.OP_PUSH_CONST and operand >= len(self.constants):
                    self.fail("constant index out of range in function %d" % index)
                if opcode == fmt.OP_ARRAY_NEW and operand > fmt.MAX_ARRAY_LENGTH:
                    self.fail("array length exceeds runtime limit in function %d" % index)
                if opcode in (fmt.OP_LOAD_LOCAL, fmt.OP_STORE_LOCAL) and operand >= function["nlocals"]:
                    self.fail("local index out of range in function %d" % index)
                if opcode == fmt.OP_CALL and operand >= len(self.functions):
                    self.fail("call target out of range in function %d" % index)
                if opcode == fmt.OP_CALL_NATIVE and operand not in fmt.NATIVE_ARITY:
                    self.fail("unknown native call in function %d" % index)
                if opcode in (fmt.OP_JMP, fmt.OP_JZ, fmt.OP_JNZ) and operand >= count:
                    self.fail("jump target out of range in function %d" % index)
            self.verify_stack(index, function, instructions, max_stack)

    def verify_stack(self, index, function, instructions, max_stack):
        """Abstractly interpret stack depth across the function's control-flow graph."""
        count = len(instructions)
        if count == 0:
            self.fail("empty function %d" % index)
            return
        depths = {}
        work = [(0, 0)]
        while work:
            pc, depth = work.pop()
            if pc < 0 or pc >= count:
                self.fail("control flow leaves function %d" % index)
                return
            if pc in depths:
                if depths[pc] != depth:
                    self.fail("inconsistent stack depth at function %d instruction %d" % (index, pc))
                    return
                continue
            depths[pc] = depth
            opcode, operand = instructions[pc]
            required = 0
            effect = 0
            terminator = False
            if opcode in (fmt.OP_PUSH_CONST, fmt.OP_PUSH_TRUE, fmt.OP_PUSH_FALSE,
                          fmt.OP_PUSH_NULL, fmt.OP_LOAD_LOCAL, fmt.OP_ARRAY_NEW):
                effect = 1
            elif opcode == fmt.OP_DUP:
                required = 1
                effect = 1
            elif opcode == fmt.OP_POP or opcode == fmt.OP_STORE_LOCAL:
                required = 1
                effect = -1
            elif opcode in (fmt.OP_ADD, fmt.OP_SUB, fmt.OP_MUL, fmt.OP_DIV, fmt.OP_MOD,
                            fmt.OP_EQ, fmt.OP_NE, fmt.OP_LT, fmt.OP_LE, fmt.OP_GT,
                            fmt.OP_GE, fmt.OP_ARRAY_GET):
                required = 2
                effect = -1
            elif opcode == fmt.OP_ARRAY_SET:
                required = 3
                effect = -3
            elif opcode in (fmt.OP_NEG, fmt.OP_NOT):
                required = 1
            elif opcode in (fmt.OP_JZ, fmt.OP_JNZ):
                required = 1
                effect = -1
            elif opcode == fmt.OP_CALL:
                if operand < len(self.functions):
                    required = self.functions[operand]["nparams"]
                    effect = 1 - required
            elif opcode == fmt.OP_CALL_NATIVE:
                arity = fmt.NATIVE_ARITY.get(operand, 0)
                required = arity
                effect = 1 - arity
            elif opcode == fmt.OP_RET:
                required = 1
                effect = -1
                terminator = True
            elif opcode in (fmt.OP_RET_VOID, fmt.OP_HALT):
                terminator = True
            if depth < required:
                self.fail("stack underflow in function %d at instruction %d" % (index, pc))
                return
            depth += effect
            if depth > max_stack:
                self.fail("stack limit exceeded in function %d" % index)
                return
            if terminator:
                continue
            if opcode == fmt.OP_JMP:
                work.append((operand, depth))
            elif opcode in (fmt.OP_JZ, fmt.OP_JNZ):
                work.append((operand, depth))
                work.append((pc + 1, depth))
            else:
                work.append((pc + 1, depth))

    def verify_metadata(self):
        if json is None:
            self.fail("JSON support is required to validate metadata")
            return
        try:
            raw = bytes(self.package[self.metadata_offset:]).decode("utf-8")
            metadata = json.loads(raw)
        except (ValueError, TypeError, UnicodeError):
            self.fail("invalid metadata JSON")
            return
        if not isinstance(metadata, dict):
            self.fail("metadata must be a JSON object")
            return
        for key in ("name", "abi", "entry", "functions", "capabilities", "resources"):
            if key not in metadata:
                self.fail("missing metadata field %s" % key)
        if metadata.get("abi") != "%d.%d" % (fmt.ABI_MAJOR, fmt.ABI_MINOR):
            self.fail("metadata ABI does not match bytecode header")
        functions = metadata.get("functions")
        if isinstance(functions, list) and len(functions) != self.header["function_count"]:
            self.fail("metadata function count does not match bytecode header")
        if isinstance(functions, list):
            entry = self.header["entry"]
            if entry >= len(functions) or metadata.get("entry") != functions[entry]:
                self.fail("metadata entry does not match bytecode header")
        capabilities = metadata.get("capabilities")
        if not isinstance(capabilities, list) or any(
            not isinstance(capability, str) for capability in capabilities
        ):
            self.fail("metadata capabilities must be a list of strings")
        resources = metadata.get("resources")
        if not isinstance(resources, dict):
            self.fail("metadata resources must be an object")
        else:
            if resources.get("max_stack", 0) > self.limits.get("max_stack", fmt.MAX_STACK_DEPTH):
                self.fail("declared stack requirement exceeds runtime limit")
            if resources.get("max_call_depth", 0) > self.limits.get("max_call_depth", fmt.MAX_CALL_DEPTH):
                self.fail("declared call-depth requirement exceeds runtime limit")
            memory_limit = resources.get("memory_limit", 0)
            allowed_memory = self.limits.get("memory_limit", 0)
            if allowed_memory and memory_limit and memory_limit > allowed_memory:
                self.fail("declared memory requirement exceeds runtime limit")
        self.metadata = metadata


def verify(package, limits=None):
    return _Verifier(package, limits).verify()
