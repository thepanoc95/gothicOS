"""IR, peephole optimization and Umbra Bytecode emission."""

import json
import struct

from . import format as fmt
from .checker import CheckError


class CompileError(Exception):
    pass


class Instruction:
    __slots__ = ("op", "operand")

    def __init__(self, op, operand=None):
        self.op = op
        self.operand = operand

    def __repr__(self):
        return "Instruction(%s, %r)" % (fmt.OPCODE_NAMES.get(self.op, self.op), self.operand)


class Label:
    __slots__ = ("name",)

    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return "Label(%s)" % self.name


class FunctionCode:
    def __init__(self, function, index):
        self.function = function
        self.index = index
        self.instructions = []
        self.local_slots = {}
        self.local_count = 0
        self.labels = {}
        self._label_counter = 0

    def emit(self, op, operand=None):
        self.instructions.append(Instruction(op, operand))
        return len(self.instructions) - 1

    def new_label(self, prefix="L"):
        self._label_counter += 1
        label = Label("%s%d" % (prefix, self._label_counter))
        self.labels[label.name] = None
        return label

    def mark(self, label):
        self.labels[label.name] = len(self.instructions)
        self.instructions.append(Instruction(fmt.OP_LABEL, label))

    def allocate(self, name):
        if name not in self.local_slots:
            self.local_slots[name] = self.local_count
            self.local_count += 1
        return self.local_slots[name]


def optimize(instructions):
    """Fold constant arithmetic without moving label markers."""
    result = [item for item in instructions if item.op != fmt.OP_NOP]
    changed = True
    while changed:
        changed = False
        index = 0
        while index + 2 < len(result):
            first, second, operation = result[index], result[index + 1], result[index + 2]
            if first.op != fmt.OP_PUSH_CONST or second.op != fmt.OP_PUSH_CONST:
                index += 1
                continue
            if not isinstance(first.operand, (int, float)) or not isinstance(second.operand, (int, float)):
                index += 1
                continue
            functions = {
                fmt.OP_ADD: lambda a, b: a + b,
                fmt.OP_SUB: lambda a, b: a - b,
                fmt.OP_MUL: lambda a, b: a * b,
                fmt.OP_DIV: lambda a, b: a / b,
                fmt.OP_MOD: lambda a, b: a % b,
            }
            if operation.op not in functions:
                index += 1
                continue
            if operation.op in (fmt.OP_DIV, fmt.OP_MOD) and second.operand == 0:
                index += 1
                continue
            value = functions[operation.op](first.operand, second.operand)
            result[index:index + 3] = [Instruction(fmt.OP_PUSH_CONST, value)]
            changed = True
    return result


class CodeGenerator:
    def __init__(self, program, checker, package_name="app"):
        self.program = program
        self.checker = checker
        self.package_name = package_name
        self.functions = []
        self.function_indices = {}
        self.constants = []
        self.constant_indices = {}
        self.scopes = []

    def generate(self):
        entry = self.checker.entry
        ordered = [entry] + [
            function for function in self.checker.functions.values() if function is not entry
        ]
        for index, function in enumerate(ordered):
            self.function_indices[function.qualified_name] = index
            self.functions.append(FunctionCode(function, index))
        for code in self.functions:
            self.compile_function(code)
        metadata = self.build_metadata()
        return self.encode(metadata)

    # --- scope and locals -------------------------------------------------

    def push_scope(self):
        self.scopes.append({})

    def pop_scope(self):
        self.scopes.pop()

    def declare_local(self, name, code):
        slot = code.allocate(name)
        self.scopes[-1][name] = slot
        return slot

    def lookup_local(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        return None

    # --- function compilation --------------------------------------------

    def compile_function(self, code):
        self.scopes = [{}]
        function = code.function
        for parameter in function.parameters:
            self.declare_local(parameter["name"], code)
        self.compile_statements(function.body, code)
        if not code.instructions or code.instructions[-1].op not in (fmt.OP_RET, fmt.OP_RET_VOID):
            code.emit(fmt.OP_PUSH_NULL)
            code.emit(fmt.OP_RET)
        code.instructions = optimize(code.instructions)

    def compile_statements(self, statements, code):
        for statement in statements:
            self.compile_statement(statement, code)

    def compile_statement(self, statement, code):
        kind = statement.kind
        if kind == "var":
            if statement.initializer is not None:
                self.compile_expression(statement.initializer, code)
            else:
                code.emit(fmt.OP_PUSH_NULL)
            code.emit(fmt.OP_STORE_LOCAL, self.declare_local(statement.name, code))
            return
        if kind == "expr":
            self.compile_expression(statement.expression, code)
            code.emit(fmt.OP_POP)
            return
        if kind == "return":
            if statement.value is None:
                code.emit(fmt.OP_PUSH_NULL)
            else:
                self.compile_expression(statement.value, code)
            code.emit(fmt.OP_RET)
            return
        if kind == "yield":
            code.emit(fmt.OP_YIELD)
            return
        if kind in ("break", "continue"):
            raise CompileError("%s is not supported by the initial embedded profile" % kind)
        if kind == "block":
            self.push_scope()
            self.compile_statements(statement.statements, code)
            self.pop_scope()
            return
        if kind == "if":
            self.compile_expression(statement.condition, code)
            else_label = code.new_label("else")
            end_label = code.new_label("endif")
            code.emit(fmt.OP_JZ, else_label)
            self.compile_statement(statement.then, code)
            code.emit(fmt.OP_JMP, end_label)
            code.mark(else_label)
            if statement.else_branch is not None:
                self.compile_statement(statement.else_branch, code)
            code.mark(end_label)
            return
        if kind == "while":
            start_label = code.new_label("while")
            end_label = code.new_label("endwhile")
            code.mark(start_label)
            self.compile_expression(statement.condition, code)
            code.emit(fmt.OP_JZ, end_label)
            self.compile_statement(statement.body, code)
            code.emit(fmt.OP_JMP, start_label)
            code.mark(end_label)
            return
        if kind == "for":
            self.push_scope()
            if statement.initializer is not None:
                self.compile_statement(statement.initializer, code)
            start_label = code.new_label("for")
            end_label = code.new_label("endfor")
            code.mark(start_label)
            if statement.condition is not None:
                self.compile_expression(statement.condition, code)
                code.emit(fmt.OP_JZ, end_label)
            self.compile_statement(statement.body, code)
            if statement.update is not None:
                self.compile_expression(statement.update, code)
                code.emit(fmt.OP_POP)
            code.emit(fmt.OP_JMP, start_label)
            code.mark(end_label)
            self.pop_scope()
            return
        raise CompileError("unsupported statement %r" % kind)

    # --- expression compilation ------------------------------------------

    def compile_expression(self, expression, code):
        kind = expression.kind
        if kind == "literal":
            code.emit(fmt.OP_PUSH_CONST, expression.value)
            return
        if kind == "identifier":
            slot = self.lookup_local(expression.name)
            if slot is None:
                raise CompileError("unbound identifier %r" % expression.name)
            code.emit(fmt.OP_LOAD_LOCAL, slot)
            return
        if kind == "binary":
            if expression.operator == "&&":
                self.compile_expression(expression.left, code)
                false_label = code.new_label("and_false")
                end_label = code.new_label("and_end")
                code.emit(fmt.OP_JZ, false_label)
                self.compile_expression(expression.right, code)
                code.emit(fmt.OP_JMP, end_label)
                code.mark(false_label)
                code.emit(fmt.OP_PUSH_FALSE)
                code.mark(end_label)
                return
            if expression.operator == "||":
                self.compile_expression(expression.left, code)
                true_label = code.new_label("or_true")
                end_label = code.new_label("or_end")
                code.emit(fmt.OP_JNZ, true_label)
                self.compile_expression(expression.right, code)
                code.emit(fmt.OP_JMP, end_label)
                code.mark(true_label)
                code.emit(fmt.OP_PUSH_TRUE)
                code.mark(end_label)
                return
            self.compile_expression(expression.left, code)
            self.compile_expression(expression.right, code)
            code.emit({
                "+": fmt.OP_ADD, "-": fmt.OP_SUB, "*": fmt.OP_MUL,
                "/": fmt.OP_DIV, "%": fmt.OP_MOD,
                "==": fmt.OP_EQ, "!=": fmt.OP_NE,
                "<": fmt.OP_LT, "<=": fmt.OP_LE,
                ">": fmt.OP_GT, ">=": fmt.OP_GE,
            }[expression.operator])
            return
        if kind == "unary":
            self.compile_expression(expression.operand, code)
            code.emit(fmt.OP_NEG if expression.operator in ("-", "+") else fmt.OP_NOT)
            return
        if kind == "array":
            if len(expression.elements) > fmt.MAX_ARRAY_LENGTH:
                raise CompileError("array literal exceeds the embedded profile limit")
            code.emit(fmt.OP_ARRAY_NEW, len(expression.elements))
            for index, element in enumerate(expression.elements):
                code.emit(fmt.OP_DUP)
                code.emit(fmt.OP_PUSH_CONST, index)
                self.compile_expression(element, code)
                code.emit(fmt.OP_ARRAY_SET)
            return
        if kind == "index":
            self.compile_expression(expression.object, code)
            self.compile_expression(expression.index, code)
            code.emit(fmt.OP_ARRAY_GET)
            return
        if kind == "assign":
            self.compile_expression(expression.value, code)
            code.emit(fmt.OP_DUP)
            target = expression.target
            if target.kind == "identifier":
                slot = self.lookup_local(target.name)
                if slot is None:
                    raise CompileError("unbound identifier %r" % target.name)
                code.emit(fmt.OP_STORE_LOCAL, slot)
            elif target.kind == "index":
                self.compile_expression(target.object, code)
                self.compile_expression(target.index, code)
                code.emit(fmt.OP_ARRAY_SET)
            else:
                raise CompileError("unsupported assignment target")
            return
        if kind == "call":
            for argument in expression.arguments:
                self.compile_expression(argument, code)
            callee = expression.callee
            if callee.kind == "identifier":
                code.emit(fmt.OP_CALL, self.function_indices[callee.function.qualified_name])
            elif callee.kind == "member":
                if getattr(callee, "function", None) is not None:
                    code.emit(fmt.OP_CALL, self.function_indices[callee.function.qualified_name])
                else:
                    native_name = callee.native_name
                    native_id = getattr(fmt, "NATIVE_" + native_name)
                    code.emit(fmt.OP_CALL_NATIVE, native_id)
            else:
                raise CompileError("unsupported call target")
            return
        raise CompileError("unsupported expression %r" % kind)

    # --- metadata and encoding -------------------------------------------

    def build_metadata(self):
        capabilities = []
        for class_node in self.program.classes:
            for decorator in class_node.decorators:
                if decorator["name"] == "Capability":
                    for argument in decorator["arguments"]:
                        capabilities.append(argument.value)
        memory_limit = 0
        for class_node in self.program.classes:
            for field in class_node.fields:
                if field["name"] == "MEMORY_LIMIT" and field.get("value") is not None:
                    memory_limit = int(field["value"])
        return {
            "name": self.package_name,
            "language": "UmbraTS",
            "language_version": "0.1",
            "abi": "%d.%d" % (fmt.ABI_MAJOR, fmt.ABI_MINOR),
            "entry": self.checker.entry.qualified_name,
            "functions": [code.function.qualified_name for code in self.functions],
            "capabilities": capabilities,
            "resources": {
                "memory_limit": memory_limit,
                "max_stack": fmt.MAX_STACK_DEPTH,
                "max_call_depth": fmt.MAX_CALL_DEPTH,
            },
        }

    def constant_index(self, value):
        tag = self.constant_tag(value)
        key = (tag, value if not isinstance(value, float) else repr(value))
        if key not in self.constant_indices:
            self.constant_indices[key] = len(self.constants)
            self.constants.append(value)
        return self.constant_indices[key]

    @staticmethod
    def constant_tag(value):
        if value is None:
            return fmt.CONST_NULL
        if isinstance(value, bool):
            return fmt.CONST_BOOL
        if isinstance(value, int):
            return fmt.CONST_INT
        if isinstance(value, float):
            return fmt.CONST_FLOAT
        if isinstance(value, str):
            return fmt.CONST_STRING
        raise CompileError("unsupported constant %r" % (value,))

    def encode_constants(self):
        blob = bytearray()
        for value in self.constants:
            tag = self.constant_tag(value)
            blob.append(tag)
            if tag == fmt.CONST_NULL:
                continue
            if tag == fmt.CONST_BOOL:
                blob.extend(struct.pack("<B", 1 if value else 0))
            elif tag == fmt.CONST_INT:
                if value < -(1 << 31) or value > (1 << 31) - 1:
                    raise CompileError("integer constant is out of range")
                blob.extend(struct.pack("<i", value))
            elif tag == fmt.CONST_FLOAT:
                blob.extend(struct.pack("<d", value))
            elif tag == fmt.CONST_STRING:
                encoded = value.encode("utf-8")
                if len(encoded) > 65535:
                    raise CompileError("string constant is too long")
                blob.extend(struct.pack("<H", len(encoded)))
                blob.extend(encoded)
        return bytes(blob)

    def encode(self, metadata):
        code_blob = bytearray()
        records = bytearray()
        for code in self.functions:
            if code.local_count > fmt.MAX_LOCALS:
                raise CompileError("too many locals in %s" % code.function.qualified_name)
            offset = len(code_blob)
            label_positions = {}
            emitted = 0
            for instruction in code.instructions:
                if instruction.op == fmt.OP_LABEL:
                    label_positions[instruction.operand.name] = emitted
                else:
                    emitted += 1
            for instruction in code.instructions:
                if instruction.op == fmt.OP_LABEL:
                    continue
                operand = instruction.operand
                if isinstance(operand, Label):
                    if operand.name not in label_positions:
                        raise CompileError("unresolved label %s" % operand.name)
                    operand = label_positions[operand.name]
                elif instruction.op == fmt.OP_PUSH_CONST:
                    operand = self.constant_index(operand)
                if operand is None:
                    operand = 0
                if operand < 0 or operand > 255:
                    raise CompileError("instruction operand out of range in %s" % code.function.qualified_name)
                code_blob.extend(struct.pack("<BB", instruction.op, operand))
            size = len(code_blob) - offset
            flags = 1 if code.function is self.checker.entry else 0
            records.extend(struct.pack(
                "<BBHHBB", code.local_count, len(code.function.parameters), offset, size, flags, 0
            ))
        constants_blob = self.encode_constants()
        metadata_blob = json.dumps(metadata, separators=(",", ":")).encode("utf-8")
        header = struct.pack(
            "<4sBBBBHHHHHHHH",
            fmt.MAGIC, fmt.VERSION_MAJOR, fmt.VERSION_MINOR,
            fmt.ABI_MAJOR, fmt.ABI_MINOR, 0,
            self.function_indices[self.checker.entry.qualified_name],
            len(self.functions), len(self.constants), len(constants_blob),
            len(code_blob), len(metadata_blob), 0,
        )
        if len(code_blob) > fmt.MAX_CODE_SIZE:
            raise CompileError("code section is too large for Umbra Bytecode v0.1")
        return header + bytes(records) + constants_blob + bytes(code_blob) + metadata_blob
