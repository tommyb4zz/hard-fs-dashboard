"""Verify that the source inventory handles real C syntax and reports missing documentation."""

import unittest

from tools.generate_function_docs import (
    collect_c_functions, collect_python_functions, collect_javascript_functions,
    collect_html_functions, resolve_edges,
)


class FunctionInventoryTests(unittest.TestCase):
    """Exercise syntax and linkage cases that can make a function graph inaccurate."""

    def test_ignores_commented_functions_and_calls_inside_strings(self):
        """Only active syntax should produce function definitions and call edges.

        The C fixture contains a commented-out function and a function-looking string. Assert only
        active definitions/calls appear, and confirm the adjacent real explanation is retained. No
        fixture code is executed.
        """
        source = b'''/* void ghost(void) { phantom(); } */
/** Return the supplied integer after calling the conversion helper. */
static int convert(int value) { puts("phantom()"); return helper(value); }
/** Return the input. */
int helper(int value) { return value; }
'''
        functions = collect_c_functions(source, "Src/example.c")
        self.assertEqual([f["name"] for f in functions], ["convert", "helper"])
        self.assertEqual(functions[0]["calls"], ["helper", "puts"])
        self.assertIn("supplied integer", functions[0]["description"])

    def test_finds_pointer_return_and_callback_reference(self):
        """Pointer-returning definitions and registered callback references are inventoried.

        Parse a pointer-returning definition whose body passes a callback identifier. Assert both real
        names and the callback reference are recorded; the test does not claim registration executes
        the callback.
        """
        source = b'''/** Handle a timer callback. */
static void tick(void *argument) { (void)argument; }
/** Register the timer and return its state pointer. */
void *start(void) { register_timer(tick); return 0; }
'''
        functions = collect_c_functions(source, "Src/timer.c")
        self.assertEqual([f["name"] for f in functions], ["tick", "start"])
        self.assertEqual(functions[1]["references"], ["tick"])

    def test_resolves_static_names_in_their_own_file(self):
        """Same-named static helpers must not create a cross-file edge.

        Combine two C files with identically named static helpers. Assert the caller resolves only to
        its own file helper, preventing a source graph from inventing cross-file linkage.
        """
        left = collect_c_functions(b'/** Left. */\nstatic void helper(void) {}\n/** Start. */\nvoid start(void) { helper(); }', "Src/left.c")
        right = collect_c_functions(b'/** Right. */\nstatic void helper(void) {}', "Src/right.c")
        edges = resolve_edges(left + right)
        self.assertEqual(edges, [{"source": left[1]["id"], "target": left[0]["id"], "kind": "call"}])

    def test_missing_comment_is_not_replaced_with_invented_description(self):
        """An undocumented function retains an explicit evidence gap.

        Parse one uncommented C definition. Assert documented is false and the literal evidence-gap
        message is preserved; no description is synthesized from its name or body.
        """
        functions = collect_c_functions(b'int undocumented(void) { return 1; }', "Src/gap.c")
        self.assertFalse(functions[0]["documented"])
        self.assertEqual(functions[0]["description"], "Insufficient data to verify")

    def test_nested_python_calls_belong_to_the_nested_function(self):
        """Defining an inner function must not imply that its calls execute in the outer function.

        Parse an outer function that defines/returns a callback. Assert the outer call set is empty and
        only the nested function contains target, distinguishing definition from execution.
        """
        functions = collect_python_functions('''def outer():
    """Return a callback without running it."""
    def inner():
        """Call the target when invoked."""
        target()
    return inner
''', "tools/example.py")
        self.assertEqual(functions[0]["calls"], [])
        self.assertEqual(functions[1]["calls"], ["target"])

    def test_production_calls_do_not_resolve_to_test_hal_fakes(self):
        """Firmware call relationships must not claim that the host-only HAL runs on the board.

        Combine a production HAL clock caller with a test-only same-name HAL definition. Assert no edge
        is emitted, because a test substitute is not the MCU implementation.
        """
        production = collect_c_functions(b'/** Poll the hardware clock. */\nvoid poll(void) { HAL_GetTick(); }', "Src/can.c")
        fake = collect_c_functions(b'/** Return a fake clock. */\nunsigned HAL_GetTick(void) { return 0; }', "tests/hal.c")
        self.assertEqual(resolve_edges(production + fake), [])

    def test_production_calls_do_not_resolve_to_dashboard_host_fakes(self):
        """A reusable production source must not imply it always uses the memory-only HAL.

        Combine production and host clock callers with a host HAL substitute. Assert only the host
        caller resolves to that substitute; reuse of production source does not change its firmware
        graph boundary.
        """
        production = collect_c_functions(b'/** Poll the clock. */\nvoid poll(void) { HAL_GetTick(); }', "Src/can.c")
        fake = collect_c_functions(b'/** Read simulated time. */\nunsigned HAL_GetTick(void) { return 0; }', "tools/dashboard_host/fake_hal.c")
        host = collect_c_functions(b'/** Step the host clock. */\nvoid step(void) { HAL_GetTick(); }', "tools/dashboard_host/main.c")
        self.assertEqual(resolve_edges(production + fake + host), [
            {"source": host[0]["id"], "target": fake[0]["id"], "kind": "call"}])

    def test_javascript_nested_scope_and_callbacks(self):
        """Inventory nested named functions and assigned arrows without attributing nested calls to their parent.

        The fixture passes a callback without executing it. Its target call belongs only to the
        callback, while the parent's direct helper call resolves within the parent's lexical scope.
        """
        functions = collect_javascript_functions(b'''/** Create a callback. */
function outer() {
  /** Call target when invoked. */
  const callback = () => target();
  /** Return a number. */
  function helper() { return 1; }
  helper();
  return callback;
}''', "tools/example.js")
        self.assertEqual([f["name"] for f in functions], ["outer", "outer.callback", "outer.helper"])
        self.assertEqual(functions[0]["calls"], ["helper"])
        self.assertEqual(functions[1]["calls"], ["target"])
        self.assertTrue(all(f["documented"] for f in functions))
        self.assertIn({"source": functions[0]["id"], "target": functions[2]["id"], "kind": "call"}, resolve_edges(functions))

    def test_javascript_same_named_helpers_are_lexical(self):
        """Same-file helper names in separate outer functions must resolve to their own scope.

        Both helper declarations are valid JavaScript names. Source adjacency is insufficient;
        resolution must use lexical scope and must not choose the helper in the other function.
        """
        functions = collect_javascript_functions(b'''/** Left scope. */
function left() { /** Left helper. */ function helper() {} helper(); }
/** Right scope. */
function right() { /** Right helper. */ function helper() {} helper(); }''', "tools/scopes.js")
        edges = resolve_edges(functions)
        self.assertEqual([(e["source"], e["target"]) for e in edges], [
            (functions[0]["id"], functions[1]["id"]),
            (functions[2]["id"], functions[3]["id"]),
        ])

    def test_javascript_anonymous_callback_has_location_and_comment(self):
        """Record an anonymous callback using a source-location label and its enclosing statement comment.

        The supplied array callback has no declared symbol to resolve. Its body call is still
        inventoried, and the comment on the statement supplies the explanation without inventing a name.
        """
        functions = collect_javascript_functions(b'/** Convert each element. */\nitems.map(value => convert(value));', "tools/callback.js")
        self.assertEqual(len(functions), 1)
        self.assertTrue(functions[0]["name"].startswith("<callback@"))
        self.assertEqual(functions[0]["calls"], ["convert"])
        self.assertEqual(functions[0]["description"], "Convert each element.")

    def test_javascript_missing_comment_remains_a_gap(self):
        """An uncommented function stays undocumented; a nearby string cannot serve as a comment."""
        functions = collect_javascript_functions(b'const note="fake()"; function gap(){ return 1; }', "tools/gap.js")
        self.assertFalse(functions[0]["documented"])
        self.assertEqual(functions[0]["description"], "Insufficient data to verify")

    def test_html_script_lines_and_noncode_data(self):
        """Only executable inline script definitions enter the graph, at their original HTML line.

        JSON text contains a function-looking string and must be ignored. Source offsets must
        include the preceding HTML so that regenerated links point to the visible definition.
        """
        source = '''<!-- Purpose: Example. -->
<script type="application/json">{"text":"function fake(){}"}</script>
<script>
/** Return one. */
function real() { return 1; }
</script>'''
        functions = collect_html_functions(source, "tools/example.html")
        self.assertEqual([(f["name"], f["line"]) for f in functions], [("real", 5)])

    def test_javascript_dynamic_members_remain_unresolved(self):
        """Member calls and duplicate global symbols cannot become an invented runtime edge.

        A dynamic object's helper member is retained as a call expression. Two separate files
        define a bare helper, so the caller's bare helper expression is ambiguous and unresolved.
        """
        caller = collect_javascript_functions(b'/** Poll object. */\nfunction poll(){ object.helper(); helper(); }', "tools/caller.js")
        one = collect_javascript_functions(b'/** First helper. */\nfunction helper(){}', "tools/one.js")
        two = collect_javascript_functions(b'/** Second helper. */\nfunction helper(){}', "tools/two.js")
        self.assertEqual(caller[0]["calls"], ["helper", "object.helper"])
        self.assertEqual(resolve_edges(caller + one + two), [])


if __name__ == "__main__":
    unittest.main()
