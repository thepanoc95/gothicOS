"""Static type checker and semantic analysis for UmbraTS."""

NUMERIC_TYPES = {
    "number", "int32", "uint32", "int64", "uint64", "float32", "float64", "uint8",
}
COMPARISON_OPERATORS = {"==", "!=", "<", "<=", ">", ">="}
LOGICAL_OPERATORS = {"&&", "||"}

NATIVE_SIGNATURES = {
    "System.log": {"parameters": ["unknown"], "returns": "void", "native": "LOG"},
    "System.info": {"parameters": [], "returns": "unknown", "native": "INFO"},
    "System.yield": {"parameters": [], "returns": "void", "native": "YIELD"},
    "Port.send": {"parameters": ["string", "string"], "returns": "boolean", "native": "PORT_SEND"},
    "Port.receive": {"parameters": ["string"], "returns": "unknown", "native": "PORT_RECEIVE"},
    "Memory.alloc": {"parameters": ["string", "string"], "returns": "uint32", "native": "MEM_ALLOC"},
    "Memory.free": {"parameters": ["string", "uint32"], "returns": "void", "native": "MEM_FREE"},
    "Interrupt.timer": {"parameters": [], "returns": "void", "native": "IRQ_TIMER"},
    "Task.stop": {"parameters": ["uint32"], "returns": "void", "native": "TASK_STOP"},
}

SDK_NAMESPACES = {"System", "Port", "Memory", "Interrupt", "Task"}


class CheckError(Exception):
    def __init__(self, message, line=None, column=None):
        self.line = line
        self.column = column
        location = "" if line is None else " at %d:%d" % (line, column)
        Exception.__init__(self, message + location)


def is_numeric(type_name):
    if type_name is None:
        return False
    return type_name in NUMERIC_TYPES


def is_boolean(type_name):
    return type_name in NUMERIC_TYPES or type_name in ("boolean", "unknown")


def base_type(type_name):
    if type_name is None:
        return None
    if type_name.endswith("[]"):
        return type_name[:-2]
    return type_name


def compatible(actual, expected):
    if expected is None or actual is None:
        return True
    if actual == "unknown" or expected == "unknown":
        return True
    if actual == expected:
        return True
    if actual.endswith("[]") and expected.endswith("[]"):
        return compatible(actual[:-2], expected[:-2])
    if is_numeric(actual) and is_numeric(expected):
        return True
    if actual == "null" and expected in ("string", "unknown"):
        return True
    return False


class Scope:
    def __init__(self, parent=None):
        self.parent = parent
        self.variables = {}

    def declare(self, name, type_name, mutable=True):
        if name in self.variables:
            raise CheckError("variable %r is already declared" % name)
        self.variables[name] = {"type": type_name, "mutable": mutable}

    def lookup(self, name):
        if name in self.variables:
            return self.variables[name]
        if self.parent is not None:
            return self.parent.lookup(name)
        return None


class Checker:
    def __init__(self, program):
        self.program = program
        self.errors = []
        self.functions = {}
        self.classes = {}
        self.entry = None
        self.aliases = dict(program.aliases)

    def report(self, message, ast_node=None):
        line = getattr(ast_node, "line", None)
        column = getattr(ast_node, "column", None)
        self.errors.append(CheckError(message, line, column))

    def resolve_type(self, type_name):
        if type_name is None:
            return None
        if type_name in self.aliases:
            return self.resolve_type(self.aliases[type_name])
        if type_name.startswith("Result<") or type_name.startswith("Promise<"):
            return "unknown"
        return type_name

    def check(self):
        for function in self.program.functions:
            self.register_function(function)
        for class_node in self.program.classes:
            self.classes[class_node.name] = class_node
            self.check_decorators(class_node.decorators, class_node)
            for method in class_node.methods:
                self.register_function(method, owner=class_node)
            for method in class_node.methods:
                if method.name == "main" and "Application" in [d["name"] for d in class_node.decorators]:
                    self.entry = method
            for field in class_node.fields:
                if not field.get("static") or not field.get("readonly") or field.get("value") is None:
                    self.report(
                        "only static readonly constant fields are supported by the embedded profile",
                        class_node,
                    )
        if self.entry is None:
            for function in self.program.functions:
                if function.name == "main":
                    self.entry = function
                    break
        if self.entry is None:
            self.report("no @Application class with static main() was found")
        else:
            if self.entry.parameters:
                self.report("entry point must not declare parameters", self.entry)
            return_type = self.resolve_type(self.entry.return_type)
            if return_type not in (None, "void", "number", "int32"):
                self.report("entry point must return void or a numeric type", self.entry)
        for name, function in self.functions.items():
            self.check_function(function)
        return self.errors

    def register_function(self, function, owner=None):
        qualified = (owner.name + "." + function.name) if owner else function.name
        if qualified in self.functions:
            self.report("duplicate function %r" % qualified, function)
            return
        function.qualified_name = qualified
        self.functions[qualified] = function

    def check_decorators(self, decorators, ast_node):
        for decorator in decorators:
            if decorator["name"] not in ("Application", "Service", "Subsystem", "Capability", "Native", "KernelInterface"):
                self.report("unknown decorator %r" % decorator["name"], ast_node)
            if decorator["name"] in ("Capability", "Native"):
                if not decorator["arguments"] or any(
                    argument.kind != "literal" or argument.literal_type != "string"
                    for argument in decorator["arguments"]
                ):
                    self.report("@%s requires string arguments" % decorator["name"], ast_node)

    def check_function(self, function):
        scope = Scope()
        for parameter in function.parameters:
            declared = self.resolve_type(parameter["type"]) or "number"
            scope.declare(parameter["name"], declared)
        self.current_return = self.resolve_type(function.return_type) or "void"
        self.check_statements(function.body, scope)

    def check_statements(self, statements, scope):
        for statement in statements:
            self.check_statement(statement, scope)

    def check_statement(self, statement, scope):
        kind = statement.kind
        if kind == "var":
            declared = self.resolve_type(statement.type)
            initializer = None
            if statement.initializer is not None:
                initializer = self.check_expression(statement.initializer, scope)
                if declared is not None and not compatible(initializer, declared):
                    self.report("cannot assign %s to %s" % (initializer, declared), statement)
            if declared is None and initializer is None:
                self.report("variable %r has no type or initializer" % statement.name, statement)
                declared = "unknown"
            scope.declare(statement.name, declared or initializer, statement.mutable)
            return
        if kind == "expr":
            self.check_expression(statement.expression, scope)
            return
        if kind == "if":
            condition = self.check_expression(statement.condition, scope)
            if not is_boolean(condition):
                self.report("if condition must be boolean", statement)
            self.check_statement(statement.then, Scope(scope))
            if statement.else_branch is not None:
                self.check_statement(statement.else_branch, Scope(scope))
            return
        if kind == "while":
            condition = self.check_expression(statement.condition, scope)
            if not is_boolean(condition):
                self.report("while condition must be boolean", statement)
            self.check_statement(statement.body, Scope(scope))
            return
        if kind == "for":
            loop_scope = Scope(scope)
            if statement.initializer is not None:
                self.check_statement(statement.initializer, loop_scope)
            if statement.condition is not None:
                condition = self.check_expression(statement.condition, loop_scope)
                if not is_boolean(condition):
                    self.report("for condition must be boolean", statement)
            if statement.update is not None:
                self.check_expression(statement.update, loop_scope)
            self.check_statement(statement.body, Scope(loop_scope))
            return
        if kind == "return":
            value_type = None
            if statement.value is not None:
                value_type = self.check_expression(statement.value, scope)
            if self.current_return == "void" and value_type is not None:
                self.report("void function cannot return a value", statement)
            elif self.current_return != "void" and value_type is None:
                self.report("function must return %s" % self.current_return, statement)
            elif value_type is not None and not compatible(value_type, self.current_return):
                self.report("cannot return %s from %s function" % (value_type, self.current_return), statement)
            return
        if kind == "block":
            self.check_statements(statement.statements, Scope(scope))
            return
        if kind in ("yield", "break", "continue"):
            return
        self.report("unsupported statement %r" % kind, statement)

    def check_expression(self, expression, scope):
        kind = expression.kind
        if kind == "literal":
            return {
                "string": "string", "bool": "boolean", "null": "null",
                "int": "int32", "float": "float64",
            }[expression.literal_type]
        if kind == "identifier":
            binding = scope.lookup(expression.name)
            if binding is None:
                self.report("unknown identifier %r" % expression.name, expression)
                return "unknown"
            if expression.name in scope.variables and not binding["mutable"]:
                expression.readonly = True
            return binding["type"]
        if kind == "member":
            if expression.object.kind != "identifier":
                self.report("member access requires a namespace or class name", expression)
                return "unknown"
            qualified = expression.object.name + "." + expression.name
            if expression.object.name in SDK_NAMESPACES:
                signature = NATIVE_SIGNATURES.get(qualified)
                if signature is None:
                    self.report("unknown SDK function %r" % qualified, expression)
                    return "unknown"
                expression.native = signature["native"]
                return "native:" + qualified
            if expression.object.name in self.classes:
                function = self.functions.get(qualified)
                if function is None:
                    self.report("unknown static method %r" % qualified, expression)
                    return "unknown"
                expression.static_function = function
                return "function:" + qualified
            self.report("unknown namespace or class %r" % expression.object.name, expression)
            return "unknown"
        if kind == "call":
            return self.check_call(expression, scope)
        if kind == "binary":
            left = self.check_expression(expression.left, scope)
            right = self.check_expression(expression.right, scope)
            operator = expression.operator
            if operator == "+" and ("string" in (left, right)):
                if left != "string" or right != "string":
                    self.report("string concatenation requires two string operands", expression)
                return "string"
            if operator in COMPARISON_OPERATORS:
                if not is_numeric(left) or not is_numeric(right):
                    self.report("comparison requires numeric operands", expression)
                return "boolean"
            if operator in LOGICAL_OPERATORS:
                if not is_boolean(left) or not is_boolean(right):
                    self.report("logical operator requires boolean operands", expression)
                return "boolean"
            if not is_numeric(left) or not is_numeric(right):
                self.report("operator %r requires numeric operands" % operator, expression)
            return "float64" if "float64" in (left, right) else "int32"
        if kind == "unary":
            operand = self.check_expression(expression.operand, scope)
            if expression.operator == "!":
                if not is_boolean(operand):
                    self.report("! requires a boolean operand", expression)
                return "boolean"
            if not is_numeric(operand):
                self.report("unary %s requires a numeric operand" % expression.operator, expression)
            return operand
        if kind == "array":
            element_types = [self.check_expression(item, scope) for item in expression.elements]
            if element_types and any(item != element_types[0] for item in element_types):
                self.report("array elements must have the same type", expression)
            return element_types[0] + "[]" if element_types else "unknown[]"
        if kind == "index":
            target = self.check_expression(expression.object, scope)
            index = self.check_expression(expression.index, scope)
            if not is_numeric(index):
                self.report("array index must be numeric", expression)
            if not target.endswith("[]"):
                self.report("indexing requires an array", expression)
                return "unknown"
            return target[:-2]
        if kind == "assign":
            target_type = self.check_expression(expression.target, scope)
            value_type = self.check_expression(expression.value, scope)
            if expression.target.kind == "identifier":
                binding = scope.lookup(expression.target.name)
                if binding is not None and not binding["mutable"]:
                    self.report("cannot assign to const %r" % expression.target.name, expression)
            if not compatible(value_type, target_type):
                self.report("cannot assign %s to %s" % (value_type, target_type), expression)
            return target_type
        self.report("unsupported expression %r" % kind, expression)
        return "unknown"

    def check_call(self, expression, scope):
        callee = expression.callee
        if callee.kind == "identifier":
            function = self.functions.get(callee.name)
            if function is None:
                self.report("unknown function %r" % callee.name, expression)
                return "unknown"
            expected = [self.resolve_type(parameter["type"]) or "number" for parameter in function.parameters]
            callee.function = function
        elif callee.kind == "member":
            member_type = self.check_expression(callee, scope)
            qualified = callee.object.name + "." + callee.name
            if member_type.startswith("native:"):
                signature = NATIVE_SIGNATURES[qualified]
                expected = signature["parameters"]
                callee.native_name = signature["native"]
            elif member_type.startswith("function:"):
                function = getattr(callee, "static_function", None)
                if function is None:
                    return "unknown"
                expected = [self.resolve_type(parameter["type"]) or "number"
                            for parameter in function.parameters]
                callee.function = function
            else:
                return "unknown"
        else:
            self.report("unsupported call target", expression)
            return "unknown"
        if len(expression.arguments) != len(expected):
            self.report(
                "%s expects %d argument(s), found %d" % (
                    callee.name if hasattr(callee, "name") else "call",
                    len(expected), len(expression.arguments),
                ),
                expression,
            )
        for index, argument in enumerate(expression.arguments):
            actual = self.check_expression(argument, scope)
            if index < len(expected) and not compatible(actual, expected[index]):
                self.report("argument %d: cannot pass %s as %s" % (index, actual, expected[index]), expression)
        if callee.kind == "identifier":
            return self.resolve_type(function.return_type) or "void"
        if getattr(callee, "function", None) is not None:
            return self.resolve_type(callee.function.return_type) or "void"
        return NATIVE_SIGNATURES[qualified]["returns"]
