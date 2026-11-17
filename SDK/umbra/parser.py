"""Recursive-descent parser for the supported UmbraTS subset."""

from .ast import BINARY_PRECEDENCE, node
from .lexer import tokenize


class ParseError(Exception):
    def __init__(self, message, token):
        self.line = token.line
        self.column = token.column
        Exception.__init__(self, "%s at %d:%d" % (message, token.line, token.column))


TYPE_TOKENS = {
    "boolean", "number", "string", "void", "null", "undefined", "never", "unknown",
    "int32", "uint32", "int64", "uint64", "float32", "float64", "uint8",
}


class Parser:
    def __init__(self, source):
        self.tokens = tokenize(source)
        self.position = 0

    def peek(self, offset=0):
        index = self.position + offset
        return self.tokens[min(index, len(self.tokens) - 1)]

    def at(self, value):
        return self.peek().value == value

    def advance(self):
        token = self.peek()
        self.position += 1
        return token

    def accept(self, value):
        if self.at(value):
            return self.advance()
        return None

    def make(self, kind, **fields):
        token = self.peek()
        return node(kind, line=token.line, column=token.column, **fields)

    def expect(self, value):
        if not self.at(value):
            raise ParseError("expected %r, found %r" % (value, self.peek().value), self.peek())
        return self.advance()

    def expect_identifier(self):
        token = self.peek()
        if token.kind != "IDENT":
            raise ParseError("expected identifier, found %r" % token.value, token)
        self.advance()
        return token.value

    def parse_program(self):
        imports = []
        functions = []
        classes = []
        aliases = {}
        while self.peek().kind != "EOF":
            self.accept("export")
            decorators = self.parse_decorators()
            self.accept("export")
            if self.at("import"):
                imports.append(self.parse_import())
            elif self.at("type"):
                name, target = self.parse_type_alias()
                aliases[name] = target
            elif self.at("class") or any(d["name"] == "Application" for d in decorators):
                classes.append(self.parse_class(decorators))
            elif self.at("function") or self.at("async"):
                functions.append(self.parse_function(decorators, static=False))
            else:
                raise ParseError("unsupported top-level construct %r" % self.peek().value, self.peek())
        return self.make("program", imports=imports, functions=functions, classes=classes, aliases=aliases)

    def parse_decorators(self):
        decorators = []
        while self.accept("@"):
            name = self.expect_identifier()
            arguments = []
            if self.accept("("):
                if not self.at(")"):
                    while True:
                        arguments.append(self.parse_expression())
                        if not self.accept(","):
                            break
                self.expect(")")
            decorators.append({"name": name, "arguments": arguments})
        return decorators

    def parse_import(self):
        self.expect("import")
        self.expect("{")
        names = []
        while not self.at("}"):
            names.append(self.expect_identifier())
            if not self.accept(","):
                break
        self.expect("}")
        self.expect("from")
        module = self.peek()
        if module.kind != "STRING":
            raise ParseError("expected module string", module)
        self.advance()
        self.accept(";")
        return {"names": names, "module": module.value}

    def parse_type_alias(self):
        self.expect("type")
        name = self.expect_identifier()
        self.expect("=")
        target = self.parse_type()
        self.accept(";")
        return name, target

    def parse_type(self):
        token = self.peek()
        if token.kind == "KEYWORD" and token.value in TYPE_TOKENS:
            self.advance()
            result = token.value
        elif token.kind == "IDENT" or token.value in ("Result", "Promise"):
            result = self.expect_identifier()
        else:
            raise ParseError("expected type, found %r" % token.value, token)
        while self.accept("["):
            self.expect("]")
            result += "[]"
        if self.accept("|"):
            result += "|" + self.parse_type()
        return result

    def parse_class(self, decorators):
        self.expect("class")
        name = self.expect_identifier()
        self.expect("{")
        methods = []
        fields = []
        while not self.at("}"):
            member_decorators = self.parse_decorators()
            static = bool(self.accept("static"))
            modifiers = []
            while self.peek().value in ("public", "private", "protected", "readonly", "abstract"):
                modifiers.append(self.advance().value)
            if self.at("function"):
                methods.append(self.parse_function(member_decorators, static))
            elif self.at("async") or (
                self.peek().kind == "IDENT" and self.peek(1).value == "("
            ):
                methods.append(self.parse_function(member_decorators, static, require_function=False))
            else:
                field_name = self.expect_identifier()
                field_type = None
                if self.accept(":"):
                    field_type = self.parse_type()
                value = None
                if self.accept("="):
                    expression = self.parse_expression()
                    if expression.kind == "literal":
                        value = expression.value
                self.accept(";")
                fields.append({
                    "name": field_name, "type": field_type, "value": value,
                    "static": static, "readonly": "readonly" in modifiers,
                })
        self.expect("}")
        return self.make("class", name=name, decorators=decorators, methods=methods, fields=fields)

    def parse_function(self, decorators, static, require_function=True):
        self.accept("async")
        if require_function:
            self.expect("function")
        name = self.expect_identifier()
        self.expect("(")
        parameters = []
        if not self.at(")"):
            while True:
                parameter_name = self.expect_identifier()
                self.accept("?")
                parameter_type = None
                if self.accept(":"):
                    parameter_type = self.parse_type()
                if self.accept("="):
                    default = self.parse_expression()
                else:
                    default = None
                parameters.append({
                    "name": parameter_name,
                    "type": parameter_type,
                    "default": default,
                })
                if not self.accept(","):
                    break
        self.expect(")")
        return_type = None
        if self.accept(":"):
            return_type = self.parse_type()
        body = self.parse_block()
        return self.make(
            "function", name=name, parameters=parameters, return_type=return_type,
            body=body, decorators=decorators, static=static,
        )

    def parse_block(self):
        self.expect("{")
        statements = []
        while not self.at("}"):
            statements.append(self.parse_statement())
        self.expect("}")
        return statements

    def parse_statement(self):
        if self.at("{"):
            return self.make("block", statements=self.parse_block())
        if self.at("let") or self.at("const"):
            mutable = self.advance().value == "let"
            name = self.expect_identifier()
            declared_type = None
            if self.accept(":"):
                declared_type = self.parse_type()
            initializer = None
            if self.accept("="):
                initializer = self.parse_expression()
            self.expect(";")
            return self.make("var", name=name, type=declared_type,
                        initializer=initializer, mutable=mutable)
        if self.at("if"):
            self.expect("if")
            self.expect("(")
            condition = self.parse_expression()
            self.expect(")")
            then_branch = self.parse_statement()
            else_branch = self.parse_statement() if self.accept("else") else None
            return self.make("if", condition=condition, then=then_branch, else_branch=else_branch)
        if self.at("while"):
            self.expect("while")
            self.expect("(")
            condition = self.parse_expression()
            self.expect(")")
            return self.make("while", condition=condition, body=self.parse_statement())
        if self.at("for"):
            self.expect("for")
            self.expect("(")
            initializer = None
            if not self.at(";"):
                if self.at("let") or self.at("const"):
                    mutable = self.advance().value == "let"
                    name = self.expect_identifier()
                    declared_type = None
                    if self.accept(":"):
                        declared_type = self.parse_type()
                    initial_value = self.parse_expression() if self.accept("=") else None
                    initializer = self.make("var", name=name, type=declared_type,
                                       initializer=initial_value, mutable=mutable)
                else:
                    initializer = self.make("expr", expression=self.parse_expression())
            self.expect(";")
            condition = None if self.at(";") else self.parse_expression()
            self.expect(";")
            update = None if self.at(")") else self.parse_expression()
            self.expect(")")
            return self.make("for", initializer=initializer, condition=condition,
                        update=update, body=self.parse_statement())
        if self.at("return"):
            self.expect("return")
            value = None if self.at(";") else self.parse_expression()
            self.expect(";")
            return self.make("return", value=value)
        if self.at("yield"):
            self.expect("yield")
            self.expect(";")
            return self.make("yield")
        if self.at("break"):
            self.expect("break")
            self.expect(";")
            return self.make("break")
        if self.at("continue"):
            self.expect("continue")
            self.expect(";")
            return self.make("continue")
        expression = self.parse_expression()
        self.expect(";")
        return self.make("expr", expression=expression)

    def parse_expression(self, minimum_precedence=1):
        left = self.parse_unary()
        while True:
            operator = self.peek().value
            precedence = BINARY_PRECEDENCE.get(operator, 0)
            if precedence < minimum_precedence:
                break
            self.advance()
            right = self.parse_expression(precedence + 1)
            left = self.make("binary", operator=operator, left=left, right=right)
        operator = self.peek().value
        if operator in ("=", "+=", "-=", "*=", "/="):
            self.advance()
            value = self.parse_expression(1)
            if operator != "=":
                value = self.make("binary", operator=operator[0], left=left, right=value)
            if left.kind not in ("identifier", "index", "member"):
                raise ParseError("invalid assignment target", self.peek())
            return self.make("assign", target=left, value=value)
        return left

    def parse_unary(self):
        if self.peek().value in ("!", "-", "+"):
            operator = self.advance().value
            return self.make("unary", operator=operator, operand=self.parse_unary())
        return self.parse_postfix()

    def parse_postfix(self):
        expression = self.parse_primary()
        while True:
            if self.accept("("):
                arguments = []
                if not self.at(")"):
                    while True:
                        arguments.append(self.parse_expression())
                        if not self.accept(","):
                            break
                self.expect(")")
                expression = self.make("call", callee=expression, arguments=arguments)
            elif self.accept("."):
                expression = self.make("member", object=expression, name=self.expect_identifier())
            elif self.accept("["):
                index = self.parse_expression()
                self.expect("]")
                expression = self.make("index", object=expression, index=index)
            elif self.peek().value in ("++", "--"):
                operator = self.advance().value
                value = self.make("binary", operator="+" if operator == "++" else "-",
                                  left=expression,
                                  right=self.make("literal", value=1, literal_type="int"))
                expression = self.make("assign", target=expression, value=value)
            else:
                break
        return expression

    def parse_primary(self):
        token = self.peek()
        if token.kind == "NUMBER":
            self.advance()
            literal = float(token.value) if "." in token.value else int(token.value, 0)
            return self.make("literal", value=literal, literal_type="float" if isinstance(literal, float) else "int")
        if token.kind == "STRING":
            self.advance()
            return self.make("literal", value=token.value, literal_type="string")
        if token.value == "true":
            self.advance()
            return self.make("literal", value=True, literal_type="bool")
        if token.value == "false":
            self.advance()
            return self.make("literal", value=False, literal_type="bool")
        if token.value in ("null", "undefined"):
            self.advance()
            return self.make("literal", value=None, literal_type="null")
        if self.accept("("):
            expression = self.parse_expression()
            self.expect(")")
            return expression
        if self.accept("["):
            elements = []
            if not self.at("]"):
                while True:
                    elements.append(self.parse_expression())
                    if not self.accept(","):
                        break
            self.expect("]")
            return self.make("array", elements=elements)
        if token.kind == "IDENT":
            self.advance()
            return self.make("identifier", name=token.value)
        raise ParseError("unexpected token %r" % token.value, token)


def parse(source):
    return Parser(source).parse_program()
