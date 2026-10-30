"""Generate a source-grounded function inventory, Mermaid graph and offline function explorer.

C and JavaScript definitions and symbolic call/reference expressions come from Tree-sitter;
Python functions come from the standard AST. Executable inline HTML scripts keep their original
source line numbers. Shell/CMake/assembly use narrowly defined source patterns. The CLI writes
JSON, Markdown, Mermaid and an offline explorer; --check also fails for missing purpose headers
or function explanations. No preprocessor conditions are evaluated. Indirect/member expressions
stay unresolved rather than becoming invented runtime calls. Upstream dependency sources in
Drivers/ and lvgl/ retain their own documentation and are outside this inventory boundary.
"""

from __future__ import annotations

import argparse
import ast
import html
import json
import re
from pathlib import Path

from tree_sitter import Language, Parser
import tree_sitter_c
import tree_sitter_javascript


def walk(node):
    """Yield node and all of its named syntax descendants in depth-first source order.

    The input is a Tree-sitter Node; no tree is changed. Anonymous punctuation is omitted
    because consumers need named definitions, calls and identifiers. This recursive iterator
    also visits nested bodies; each collector decides which nodes belong to its own function.
    """
    yield node
    for child in node.named_children:
        yield from walk(child)


def clean_comment(value):
    """Return readable explanation text from a C/JS/shell-style comment string.

    Strip outer block delimiters, line-leading comment markers and the @brief tag; join
    nonempty lines with spaces for JSON/table display. Other Doxygen content is retained.
    This formats existing evidence rather than manufacturing a description or interpreting HTML.
    """
    value = re.sub(r"^/\*+|\*/$", "", value.strip())
    lines = [re.sub(r"^\s*(?:\*|//)\s?", "", line).strip() for line in value.splitlines()]
    return " ".join(line.replace("@brief", "").strip() for line in lines if line)


def collect_c_functions(source, file):
    """Extract C definitions, adjacent comments and symbolic calls/references from UTF-8 bytes.

    file is the repository-relative source name used in IDs and links. Tree-sitter distinguishes
    active syntax from commented-out code and strings; pointer-returning declarators are followed
    to their actual identifier. Consecutive adjacent comments form the description. A missing
    comment produces documented=False and an explicit evidence gap, never a guessed explanation.

    Return dictionaries with source line, linkage and calls/references. Conditional branches
    remain present because preprocessing is not evaluated. No source is executed or modified.
    An unidentifiable declarator raises ValueError. Indirect/member targets stay expressions.
    """
    tree = Parser(Language(tree_sitter_c.language())).parse(source)
    functions = []
    for node in walk(tree.root_node):
        if node.type != "function_definition":
            continue
        declarator = node.child_by_field_name("declarator")
        while declarator is not None and declarator.type != "identifier":
            declarator = declarator.child_by_field_name("declarator")
        if declarator is None:
            raise ValueError(f"Cannot identify function at {file}:{node.start_point.row + 1}")
        name = declarator.text.decode()
        previous = node.prev_named_sibling
        comments = []
        boundary = node.start_byte
        while previous is not None and previous.type == "comment":
            if source[previous.end_byte:boundary].strip():
                break
            comments.insert(0, previous.text.decode())
            boundary = previous.start_byte
            previous = previous.prev_named_sibling
        body = node.child_by_field_name("body")
        calls = set()
        referenced = set()
        for child in walk(body):
            if child.type == "call_expression":
                target = child.child_by_field_name("function")
                if target is not None:
                    calls.add(target.text.decode())
            if child.type == "identifier":
                referenced.add(child.text.decode())
        header = source[node.start_byte:body.start_byte].decode()
        functions.append({
            "id": f"{file}::{name}:{node.start_point.row + 1}",
            "name": name, "file": file, "line": node.start_point.row + 1,
            "language": "C", "static": bool(re.search(r"\bstatic\b", header)),
            "documented": bool(comments),
            "description": clean_comment("\n".join(comments)) if comments else "Insufficient data to verify",
            "calls": sorted(calls), "references": sorted(referenced - calls - {name}),
        })
    return functions


def collect_python_functions(source, file):
    """Inventory Python functions/methods from source text, with lexical names and AST call targets.

    file supplies relative path/IDs. Parse with ast, visit classes/nested scopes, read a function
     docstring or contiguous preceding comments, and return definitions sorted by source line.
    Class and outer-function prefixes distinguish methods/nested functions. Calls inside a nested
    definition belong to that nested function rather than its enclosing body.

    Attribute calls remain spelled expressions; this collector does not infer object types or
    execute decorators/functions. ast syntax errors propagate. Missing explanations retain an
    explicit evidence gap. The returned inventory is new data; the input source is unchanged.
    """
    tree = ast.parse(source)
    functions = []
    lines = source.splitlines()

    def body_calls(node):
        """Return the set of call-expression spellings executed within this AST subtree.

        Do not descend through function/class/lambda definitions, since defining a callable does not
        execute its body. Ordinary statement/expression descendants contribute calls via ast.unparse.
        This is symbolic source evidence, not control-flow evaluation or a claim every branch runs.
        """
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            return set()
        calls = {ast.unparse(node.func)} if isinstance(node, ast.Call) else set()
        for child in ast.iter_child_nodes(node):
            calls.update(body_calls(child))
        return calls

    def visit(node, prefix=""):
        """Append definitions while traversing lexical Python scopes under prefix.

        Class names and enclosing function names extend the prefix. For each function, collect calls
        from its statements, obtain its own docstring/preceding comments and append source metadata.
        Then visit its nested definitions separately. Mutates only the collector's local functions
        list; no project class/function is instantiated or invoked.
        """
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.ClassDef):
                visit(child, prefix + child.name + ".")
            elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                name = prefix + child.name
                calls = set().union(*(body_calls(statement) for statement in child.body))
                description = ast.get_docstring(child)
                if not description:
                    before = child.lineno - 2
                    comments = []
                    while before >= 0 and (not lines[before].strip() or lines[before].lstrip().startswith("#")):
                        comments.insert(0, lines[before].lstrip().lstrip("#").strip())
                        before -= 1
                    description = " ".join(line for line in comments if line)
                functions.append({
                    "id": f"{file}::{name}:{child.lineno}", "name": name,
                    "file": file, "line": child.lineno, "language": "Python", "static": True,
                    "documented": bool(description), "description": " ".join(description.split()) if description else "Insufficient data to verify",
                    "calls": sorted(calls), "references": [],
                })
                visit(child, name + ".")
            else:
                visit(child, prefix)

    visit(tree)
    return sorted(functions, key=lambda item: item["line"])


def collect_javascript_functions(source, file, line_offset=0):
    """Inventory JavaScript declarations, methods, arrows and callbacks using actual syntax.

    source is UTF-8 bytes from a JS file or one executable HTML script; line_offset restores
    the original HTML location. Nested body calls belong only to the nested function. Names
    carry lexical scopes; unnamed callbacks use a location label rather than an invented
    callable symbol. Comments on enclosing declarations/statements can document arrows.
    No object-member target or callback execution is inferred. Syntax errors raise ValueError.
    """
    tree = Parser(Language(tree_sitter_javascript.language())).parse(source)
    if tree.root_node.has_error:
        raise ValueError(f"JavaScript syntax error in {file}")
    function_types = {"function_declaration", "function_expression", "generator_function_declaration",
                      "generator_function", "arrow_function", "method_definition"}
    functions = []
    names = {}

    def adjacent_description(node):
        """Read adjacent comments, climbing only the arrow's enclosing declaration/statement.

        A containing function is a documentation boundary, so its description cannot silently
        describe an otherwise undocumented callback. A comment at the start of a callback
        body may supply that callback's own explanation when no statement comment exists.
        """
        cursor = node
        while cursor is not None:
            previous, boundary, comments = cursor.prev_named_sibling, cursor.start_byte, []
            while previous is not None and previous.type == "comment":
                if source[previous.end_byte:boundary].strip():
                    break
                comments.insert(0, previous.text.decode())
                boundary, previous = previous.start_byte, previous.prev_named_sibling
            if comments:
                return clean_comment("\n".join(comments))
            parent = cursor.parent
            if parent is None or parent.type in function_types or parent.type in ("program", "statement_block"):
                break
            cursor = parent
        body = node.child_by_field_name("body")
        if body is not None and body.named_children and body.named_children[0].type == "comment":
            return clean_comment(body.named_children[0].text.decode())
        return ""

    def body_expressions(node, calls, references):
        """Collect one function's call expressions/identifiers, excluding nested callable bodies.

        Member expressions are retained verbatim as unresolved targets. Plain identifiers are
        references only; their presence does not establish that a callback was executed.
        """
        if node.type in function_types:
            return
        if node.type == "call_expression":
            target = node.child_by_field_name("function")
            if target is not None:
                calls.add(target.text.decode())
        if node.type == "identifier":
            references.add(node.text.decode())
        for child in node.named_children:
            body_expressions(child, calls, references)

    for node in walk(tree.root_node):
        if node.type not in function_types:
            continue
        declared = node.child_by_field_name("name")
        if declared is None and node.parent is not None and node.parent.type == "variable_declarator":
            declared = node.parent.child_by_field_name("name")
        symbol = declared.text.decode() if declared is not None else ""
        line = node.start_point.row + 1 + line_offset
        label = symbol or f"<callback@{line}:{node.start_point.column + 1}>"
        enclosing = node.parent
        while enclosing is not None and enclosing.type not in function_types:
            enclosing = enclosing.parent
        scope = names.get(enclosing.start_byte, "") if enclosing is not None else ""
        name = scope + "." + label if scope else label
        names[node.start_byte] = name
        calls, references = set(), set()
        body = node.child_by_field_name("body")
        if body is not None:
            # The root body may itself be an expression; only descendants that are functions
            # are excluded, not ordinary call-expression bodies in concise arrows.
            body_expressions(body, calls, references)
        description = adjacent_description(node)
        functions.append({
            "id": f"{file}::{name}:{line}", "name": name, "symbol": symbol, "scope": scope,
            "file": file, "line": line, "language": "JavaScript", "static": False,
            "documented": bool(description), "description": description or "Insufficient data to verify",
            "calls": sorted(calls), "references": sorted(references - calls - {symbol}),
        })
    return functions


def collect_html_functions(source, file):
    """Collect executable inline-script functions with source lines relative to the HTML file.

    This extractor handles the repository's static script elements. JSON/data script types
    and scripts with src attributes are not parsed as inline JavaScript. No remote script
    is fetched; template placeholders in application/json remain untouched and unexecuted.
    """
    functions = []
    for match in re.finditer(r"<script\b([^>]*)>([\s\S]*?)</script\s*>", source, re.IGNORECASE):
        attributes = match[1]
        type_match = re.search(r"\btype\s*=\s*['\"]([^'\"]*)['\"]", attributes, re.IGNORECASE)
        if re.search(r"\bsrc\s*=", attributes, re.IGNORECASE):
            continue
        if type_match and type_match[1].lower() not in ("module", "text/javascript", "application/javascript"):
            continue
        offset = source[:match.start(2)].count("\n")
        functions.extend(collect_javascript_functions(match[2].encode(), file, offset))
    return functions


def collect_other_functions(source, file):
    """Return documented shell/CMake/assembly definitions using narrow line-based patterns.

    source is text and file chooses the language. Shell recognizes name() {; CMake recognizes
    function(...); assembly recognizes labels only when a function type/size declaration exists,
    excluding declared objects and internal loop labels. Adjacent preceding comments supply text.

    These records contain no inferred calls. Pattern coverage is intentionally limited and does
    not preprocess assembly or parse a shell/CMake runtime. Source lines and language identify
    where to inspect the full body. Missing explanations are explicit rather than generated.
    """
    if file.endswith(".sh"):
        pattern, language = r"^([A-Za-z_]\w*)\s*\(\s*\)\s*\{", "Shell"
    elif file.endswith(".cmake") or file.endswith("CMakeLists.txt"):
        pattern, language = r"^function\(\s*([A-Za-z_]\w*)", "CMake"
    else:
        pattern, language = r"^([A-Za-z_]\w*):", "Assembly"
    lines = source.splitlines()
    functions = []
    for index, line in enumerate(lines):
        match = re.match(pattern, line, re.IGNORECASE)
        if not match:
            continue
        if language == "Assembly":
            symbol = re.escape(match[1])
            if re.search(r"\.type\s+" + symbol + r"\s*,\s*%object", source):
                continue
            if not re.search(r"\.type\s+" + symbol + r"\s*,\s*%function|\.size\s+" + symbol + r"\s*,\s*\.-" + symbol, source):
                continue
        comment_lines = []
        before = index - 1
        while before >= 0 and (not lines[before].strip() or lines[before].lstrip().startswith(("#", "/*", "*", "//"))):
            comment_lines.insert(0, lines[before].lstrip("# "))
            before -= 1
        description = clean_comment("\n".join(comment_lines))
        functions.append({
            "id": f"{file}::{match[1]}:{index + 1}", "name": match[1], "file": file,
            "line": index + 1, "language": language, "static": True,
            "documented": bool(description), "description": description or "Insufficient data to verify",
            "calls": [], "references": [],
        })
    return functions


def resolve_edges(functions):
    """Return sorted, unambiguous symbolic call/reference edges between inventory definitions.

    Match language and declared names; prefer file-local C symbols. JS resolution uses visible
    lexical scopes and retains dynamic members as unresolved expressions. Duplicate eligible
    symbols yield no edge. Production files cannot resolve to tests or desktop HAL substitutes;
    host/test files may resolve their appropriate local targets.

    The input dictionaries are not changed. A reference edge means a symbol occurs/passes in
    source, not that a callback executes. Cross-language startup/interrupt connections require
    the inspected architecture guide and are not manufactured from same-name matches.
    """
    edges = []
    for function in functions:
        for kind, names in (("call", function["calls"]), ("reference", function["references"])):
            for name in names:
                if function["language"] == "JavaScript":
                    candidates = [target for target in functions if target["language"] == "JavaScript" and target.get("symbol") == name]
                    visible = [target for target in candidates if target["file"] == function["file"] and
                               (not target["scope"] or function["name"] == target["scope"] or function["name"].startswith(target["scope"] + "."))]
                    if visible:
                        depth = max(len(target["scope"]) for target in visible)
                        candidates = [target for target in visible if len(target["scope"]) == depth]
                    else:
                        candidates = [target for target in candidates if not target["scope"]]
                else:
                    candidates = [target for target in functions if target["name"] == name and target["language"] == function["language"]]
                if not function["file"].startswith("tests/"):
                    candidates = [target for target in candidates if not target["file"].startswith("tests/")]
                if not function["file"].startswith(("tests/", "tools/dashboard_host/")):
                    candidates = [target for target in candidates if not target["file"].startswith("tools/dashboard_host/")]
                local = [target for target in candidates if target["file"] == function["file"]]
                candidates = local or [target for target in candidates if not target["static"]]
                if len(candidates) == 1:
                    edges.append({"source": function["id"], "target": candidates[0]["id"], "kind": kind})
    return sorted(edges, key=lambda edge: (edge["source"], edge["target"], edge["kind"]))


def project_sources(root):
    """Return a sorted, deduplicated list of maintained source/script/template Paths under root.

    Scan application, headers, generated UI, tools, tests, CMake and editor scripts; include the
    explicit root exporter/build/LVGL-config/linker files. Recognized extensions include JS/HTML
    so browser functions are auditable. Skip Python caches, vendor Drivers/lvgl and local build
    outputs. This boundary is a documentation choice, not a claim dependencies contain no code.
    """
    paths = []
    for directory in ("Src", "Inc", "ui", "tools", "tests", "cmake", ".vscode"):
        if (root / directory).exists():
            paths.extend((root / directory).rglob("*"))
    paths.extend([root / "dump_emmc_bin.py", root / "CMakeLists.txt", root / "lv_conf.h", root / "stm32h750xb_flash.ld"])
    return sorted({path for path in paths if path.is_file() and (path.suffix in (".c", ".h", ".py", ".sh", ".cmake", ".S", ".ld", ".js", ".html") or path.name == "CMakeLists.txt") and "__pycache__" not in path.parts})


def collect_inventory(root):
    """Read maintained sources beneath root and return definitions, resolved edges and header gaps.

    Choose the language parser from each path; HTML parses executable inline scripts only.
    Check a leading comment/docstring after an optional shebang for a purpose header. A header
    check verifies presence, while the human guide/review establishes explanatory quality.

    No firmware or synthetic scenario runs here. File/encoding/parser errors propagate rather
    than silently omitting a file. The resulting dictionaries feed all generated documentation;
    missing source comments remain visible and can fail the CLI's --check mode.
    """
    functions = []
    missing_headers = []
    for path in project_sources(root):
        file = path.relative_to(root).as_posix()
        source = path.read_text()
        first = source.lstrip()
        if first.startswith("#!"):
            first = first.split("\n", 1)[-1].lstrip()
        prefixes = ('"""', "'''", "#") if path.suffix == ".py" else (("<!--",) if path.suffix == ".html" else (("#",) if path.suffix in (".sh", ".cmake") or path.name == "CMakeLists.txt" else ("/*", "//")))
        if not first.startswith(prefixes):
            missing_headers.append(file)
        if path.suffix in (".c", ".h"):
            functions.extend(collect_c_functions(source.encode(), file))
        elif path.suffix == ".py":
            functions.extend(collect_python_functions(source, file))
        elif path.suffix == ".js":
            functions.extend(collect_javascript_functions(source.encode(), file))
        elif path.suffix == ".html":
            functions.extend(collect_html_functions(source, file))
        elif path.suffix in (".sh", ".cmake", ".S") or path.name == "CMakeLists.txt":
            functions.extend(collect_other_functions(source, file))
    return {"functions": functions, "edges": resolve_edges(functions), "missing_purpose_headers": missing_headers}


def write_outputs(inventory, output):
    """Create output and write the inventory's JSON, Markdown index, Mermaid graph and offline HTML.

    The inventory is already collected/resolved. Stable source IDs map to Mermaid nodes and file
    subgraphs; call and reference edges use distinct arrows. Markdown escapes table delimiters.
    The explorer template receives inert JSON with '<' escaped so source strings cannot close
    its data script tag. Only these generated files are replaced; project implementations and
    upstream sources are untouched. Filesystem/template errors propagate to the caller.
    """
    output.mkdir(parents=True, exist_ok=True)
    functions, edges = inventory["functions"], inventory["edges"]
    (output / "functions.json").write_text(json.dumps(inventory, indent=2) + "\n")
    lines = ["# Project function inventory", "", "Generated from source definitions and adjacent comments. Includes conditional definitions without evaluating preprocessor branches. Calls to dependencies are retained in the JSON; indirect execution is not inferred. `reference` edges show a symbol passed or assigned, including callbacks. `Drivers/` and `lvgl/` retain upstream documentation.", "", "Regenerate with `.venv/bin/python tools/generate_function_docs.py --check`.", ""]
    for file in sorted({function["file"] for function in functions}):
        lines.extend([f"## {file}", "", "| Function | Line | Explanation from source |", "|---|---:|---|"])
        for function in functions:
            if function["file"] == file:
                description = function["description"].replace("|", "\\|").replace("\n", " ")
                lines.append(f"| `{function['name']}` | [{function['line']}](../{file}#L{function['line']}) | {description} |")
        lines.append("")
    (output / "FUNCTIONS.md").write_text("\n".join(lines))
    node_ids = {function["id"]: f"f{index}" for index, function in enumerate(functions)}
    graph = ["flowchart LR"]
    for index, file in enumerate(sorted({function["file"] for function in functions})):
        graph.append(f'  subgraph s{index}["{html.escape(file)}"]')
        for function in functions:
            if function["file"] == file:
                graph.append(f'    {node_ids[function["id"]]}["{html.escape(function["name"])} :{function["line"]}"]')
        graph.append("  end")
    for edge in edges:
        arrow = "-->" if edge["kind"] == "call" else "-. reference .->"
        graph.append(f'  {node_ids[edge["source"]]} {arrow} {node_ids[edge["target"]]}')
    (output / "functions.mmd").write_text("\n".join(graph) + "\n")
    template = Path(__file__).with_name("function_explorer.html").read_text()
    (output / "function-explorer.html").write_text(template.replace("__INVENTORY_JSON__", json.dumps(inventory).replace("<", "\\u003c")))


def main():
    """Parse documentation CLI options, collect source evidence, write artifacts and return a status.

    --root defaults to this repository; --output defaults to its docs folder. --check turns any
    missing function description or file-purpose header into exit code 1, while still producing
    the inspectable inventory. Without --check, gaps are printed but return code is zero.

    Print definition/edge counts and gap locations. Filesystem/parser failures propagate rather
    than being treated as successful coverage. The module entry point converts the result into
    SystemExit; this command does not compile, flash or execute inventoried application code.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    inventory = collect_inventory(args.root)
    write_outputs(inventory, args.output or args.root / "docs")
    gaps = [f"{function['file']}:{function['line']} {function['name']}" for function in inventory["functions"] if not function["documented"]]
    gaps.extend("Missing purpose header: " + file for file in inventory["missing_purpose_headers"])
    print(f"Inventoried {len(inventory['functions'])} functions and {len(inventory['edges'])} resolved call/reference edges.")
    if gaps:
        print("\n".join(gaps))
    return 1 if args.check and gaps else 0


if __name__ == "__main__":
    raise SystemExit(main())
