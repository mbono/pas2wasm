# Pas2Wasm language reference

This reference describes the Pascal subset accepted by both compiler builds. Both emit
WebAssembly; generated programs use the supplied host runtime. The subset includes
extensions and does not claim full ISO 7185 conformance.

## Contents

- [Program structure and names](#program-structure-and-names)
- [Declarations](#declarations)
- [Types and expressions](#types-and-expressions)
- [Statements and control flow](#statements-and-control-flow)
- [Procedures and functions](#procedures-and-functions)
- [Arrays and records](#arrays-and-records)
- [Strings and characters](#strings-and-characters)
- [Typed pointers](#typed-pointers)
- [Checked increment](#checked-increment)
- [Input and output](#input-and-output)
- [Example playground console](#example-playground-console)
- [Storage, limits, and diagnostics](#storage-limits-and-diagnostics)
- [Unsupported features](#unsupported-features)
- [Formal grammar](#formal-grammar)

## Program structure and names

```pascal
program sum;
var
  a, b: integer;
begin
  readln(a, b);
  writeln(a + b)
end.
```

Declaration sections are optional and appear before the main `begin ... end.`:

```text
program name;
[source-ordered const/type/var sections and routine declarations]
begin
  statements
end.
```

| Topic | Rule | Example |
| --- | --- | --- |
| Case | Names and keywords are case-insensitive. | `Total` and `total` name the same declaration. |
| Identifiers | ASCII letters, digits, and underscores; start with a letter or underscore. | `total_count`, `_count2`, `_` |
| Underscores | Significant; `_` is an ordinary name, not a discard placeholder. This extends ANSI/ISO 7185 Pascal. | `ab` and `a_b` are different names. |
| Reserved names | Keywords and built-in names cannot be used as identifiers. | `length`, `record`, `new` |
| Source encoding | UTF-8, LF/CRLF line endings, optional BOM. | Diagnostic columns count UTF-8 bytes from 1. |
| Comments | Braces or parenthesized stars; no nesting. | `{ note }`, `(* note *)` |
| Text literals | Single quotes; double a quote to escape it. | `'Pascal'`, `'don''t'` |
| Scope | Globals have program-wide scope; each routine call has private locals. | Local declarations may shadow outer ones. |
| Initialization | Variables must be initialized before reading. | `n := 1; writeln(n)` |

Keywords match whole tokens: `begin_` is an identifier. `//` comments, nested comments,
and program parameter lists such as `(input, output)` are unsupported. Unterminated
comments/literals are located compilation errors. Integer literals are decimal;
`-2147483648` is valid while positive `2147483648` is out of range.

## Declarations

| Declaration | Example | Meaning |
| --- | --- | --- |
| Constant | `const size = 2 * 3;` | Evaluate once during compilation. |
| Type alias | `type Count = integer;` | Another name for the same type. |
| Variable | `var count: integer;` | Writable storage, initially uninitialized. |
| Procedure | `procedure show(n: integer);` | Routine without a result. |
| Function | `function double(n: integer): integer;` | Routine with a typed result. |

Each declaration section is optional but must be nonempty when present. Local sections
follow a routine header, after parameters/implicit result enter scope. Value, type,
constant, result, and routine declarations share lexical lookup; record fields have
their own namespace.

### Constants

Declare global or local constants before using them:

```pascal
const
  size = 2 * 3;
  ready = size > 0;
```

| Rule | Details |
| --- | --- |
| Evaluation | Checked arithmetic and eager Boolean evaluation, as at runtime. |
| Allowed references | Earlier visible constants; no variables, routine calls, forward references, or cycles. |
| Scope | Locals may shadow globals; duplicate names in one scope are errors. |
| Storage | No runtime variable slots. Constants cannot be assigned, read into, or used as `for` controls. |
| Errors | Integer overflow uses `E_INTEGER`; invalid division uses `E_CONST_DIV_ZERO` or `E_CONST_DIV_DOMAIN`. |

Using variables/routine calls or modifying constants uses `E_CONST`; unknown, cyclic, or
forward constant names use `E_UNKNOWN`. Same-scope duplicates use `E_DUPLICATE`.
Built-in constant operations are covered by their type sections, including real
literals/conversions and string/character operations.

### Type aliases

```pascal
type
  Count = integer;
  Flag = boolean;
  Other = Count;
```

Aliases share their target's identity: `Count` and `integer` are compatible. They work
in variables, parameters, and function return types without conversions or extra runtime
storage. Type names are not values.

Const/type/var sections and routines may repeat and interleave before the block body.
Declarations become visible in source order; earlier routine bodies cannot use later
declarations. Local sections follow the routine header. Only earlier
visible type names are allowed, except for the pointer targets described below.
Duplicate names in one scope use `E_DUPLICATE`; locals may shadow globals. Unknown
by-value, self, or forward type names use `E_UNKNOWN`; non-type aliases or type names
used as values use `E_TYPE`. `..` is one range token, distinct from `.`; ranges are not
ordinary expression values.

## Types and expressions

| Type | Values / representation | Example |
| --- | --- | --- |
| `integer` | Signed 32-bit: −2147483648 through 2147483647. | `42`, `-7` |
| `boolean` | Distinct Boolean values. | `true`, `false` |
| `real` | Finite binary64 numbers. | `1.5`, `1e2`, `1.5E-2` |
| `char` | One Unicode scalar, excluding surrogate values. | `chr(128512)` |
| `string[N]` | At most N UTF-8 bytes; bare `string` means 255 bytes. | `string[20]` |
| Fixed array | Constant integer bounds and one element type. | `array[-2..2] of integer` |
| Record | Named fields with declared types. | `record x, y: integer end` |
| Typed pointer | Reference to a declared pointee type, or `nil`. | `^Node` |

### Operators and evaluation

The table lists numeric and Boolean operator groups from higher to lower precedence.
Parentheses select an explicit grouping.

| Group | Operators | Rules |
| --- | --- | --- |
| Factor | `not` | Boolean negation; applies to a factor. |
| Multiplicative | `*`, `/`, `div`, `mod`, `and` | `/` performs real division even for integer operands. |
| Additive | `+`, `-`, `or` | Unary `+`/`-` start a simple expression. |
| Comparison | `=`, `<>`, `<`, `<=`, `>`, `>=` | Booleans use `false < true`; chained comparisons are rejected. |

Binary operators at the same precedence associate left-to-right. Operands and routine
arguments evaluate left-to-right. Boolean `and`/`or` evaluation is eager. Use
parentheses when combining comparisons, for example `(a > 0) and (b > 0)`.

Integer overflow and uninitialized reads fail explicitly. `div` and `mod` require a
nonnegative dividend and a positive divisor. Only integer-to-real numeric widening is
implicit. String and character operators are described below.

### Real numbers and conversions

| Conversion | Result | Rule |
| --- | --- | --- |
| `real(value)` | Real | Widen an integer or retain a real value. |
| `trunc(value)` | Integer | Accept integer/real input; round toward zero with range checks. |
| `round(value)` | Integer | Accept integer/real input; round to nearest, with ties to even and range checks. |

Reals use finite binary64: `1.5`, `1e2`, and `1.5E-2`. Leading digits and fractional
digits after a dot are required; `1..5` remains range syntax. Mixed numeric +,-,*,/ and
comparisons widen integers exactly; integer-only arithmetic/div/mod retain checks. Real
assignments/value arguments/results accept integers, while var arguments require exact
types. Booleans remain distinct. Narrowing outside signed 32-bit range fails with
`R_CONVERSION` (`E_CONVERSION` in constants); no implicit narrowing occurs. Constants
support real operators, widening, and conversions with runtime types/evaluation order.
Literal conversion rounds the exact decimal to nearest/ties-even, independent of locale.

Division by either signed zero fails. Nonfinite results use `R_REAL_RANGE`; subnormal
arithmetic and signed zero are preserved. Nonzero literal underflow/overflow uses
`E_REAL_RANGE`; literals are bounded by 768 significand digits and exponent magnitude
4,096 (`E_LIMIT`). Malformed exponents use `E_REAL_LITERAL`. `.5` and `1.` are not real
literals; NaN/infinity syntax is unsupported. Division by signed zero uses `R_DIV_ZERO`
(`E_CONST_DIV_ZERO` in constants). Negative zero compares equal to zero. Nonfinite
constant arithmetic uses `E_REAL_RANGE`. Real input parsing is described under [Input
and output](#input-and-output). Output is deterministic, unpadded, round-tripping
decimal with `-0` preserved; e.g. `1/2` prints `0.5`. Raw modules require the matching
ABI-v1 runtime; exported HTML embeds it and remains self-contained.

Real output tries explicit precisions 1 through 17 and selects the first whose
`abs(value).toExponential(precision - 1)` parses back to the same binary64. Strip
redundant fractional zeros; use fixed notation for decimal exponents -6 through 20 and
signed `e` notation otherwise. Preserve `-0` explicitly. This freezes decimal tie
selection; FPC's default real formatting intentionally differs.

## Statements and control flow

| Statement | Syntax example | Behavior |
| --- | --- | --- |
| Assignment | `n := n + 1` | Store a compatible value. |
| Block | `begin first; second end` | Group statements. |
| Conditional | `if ready then run else wait` | Select using a Boolean condition. |
| Pre-test loop | `while n > 0 do n := n - 1` | Test before each iteration. |
| Post-test loop | `repeat n := n - 1 until n = 0` | Run at least once; stop when the condition is true. |
| Counted loop | `for i := 1 to 3 do writeln(i)` | Inclusive integer bounds; `downto` counts downward. |
| Selection | `case n of 1: writeln('one') end` | Execute one matching arm, without fall-through. |
| Loop exit | `break` | Leave the nearest enclosing loop. |
| Next iteration | `continue` | Continue the nearest enclosing loop. |
| Routine exit | `exit`, `exit(value)` | Leave a routine or main program; value form is for functions. |
| Empty statement | `begin end` | Do nothing. |

Semicolons separate statements. A trailing semicolon before `end` is allowed; a
semicolon before an `if` statement's `else` is an error. An `else` belongs to the
nearest unmatched `if`.

All `if`, `while`, and `repeat` conditions must be Boolean.

`repeat` accepts multiple statements, an optional trailing semicolon, or an empty body
without requiring a `begin/end` wrapper. Its condition must be Boolean.

### Counted loops

`for i := start to finish do statement` counts upward by 1; `downto` counts downward by
1. The control variable and bounds must be integers. Bounds are inclusive and evaluated
once, start then finish, before assigning the control variable. A range facing away from
its final bound has no iterations. Reaching an integer endpoint does not overflow. The
body cannot assign the control variable, read into it, or reuse it in a nested `for`
(`E_FOR_CONTROL`). After any completed loop, including an empty range, its control
variable is uninitialized; assign it before reading it again. Other nested loops may use
distinct variables. Direct violations are rejected even in unreachable bodies. Bounds
may read an already initialized control value; the initial loop assignment does not
initialize it during bound evaluation.

`break` exits the nearest enclosing loop. `continue` skips to the nearest loop's
condition (`while`/`repeat`) or completion/step logic (`for`). Both require a loop;
otherwise compilation fails with `E_LOOP_CONTROL`. Breaking a `for` also leaves its
control variable uninitialized. Nested blocks and `if` do not change targets.

### Case selection

```pascal
case n of
  1, 2: writeln('small');
  3..5: writeln('medium');
else
  writeln('other')
end
```

| Rule | Details |
| --- | --- |
| Selector | An integer or Boolean expression, evaluated once, even for empty arms. |
| Labels | Matching literals or named scalar constants; signed integers are allowed. At least one labeled arm is required. |
| Integer ranges | Inclusive `low..high`; mix ranges and single labels, such as `low..high, 9`. Boolean ranges are rejected. |
| Matching | Execute one arm without fall-through. With no match, run the optional `else` list or do nothing. |
| Multiple statements | Wrap an arm in `begin/end`; the fallback list needs no wrapper. |
| Semicolons | A trailing semicolon before case `else`/`end` is accepted. An unmatched inner `if` takes its nearest `else`; separate a case fallback with a semicolon. |
| Transfers | `break`/`continue` still target enclosing loops; `exit` leaves the routine. |
| Invalid labels | Duplicate labels or overlapping ranges use `E_DUPLICATE`; type mismatches or signs on Boolean constants use `E_TYPE`; reversed ranges use `E_CASE_RANGE`. |
| Name/value checks | Signed zero spellings are duplicates; variables/calls use `E_CONST`, unknown names use `E_UNKNOWN`. |
| Limit | 1,024 labels/ranges per selection (`E_LIMIT`); each range counts as one, including at integer extremes. |

## Procedures and functions

```pascal
procedure show(n: integer);
begin
  writeln(n)
end;

function double(n: integer): integer;
begin
  double := n * 2
end;
```

| Feature | Form | Key rule |
| --- | --- | --- |
| Procedure call | `show(3)` | Used as a statement. |
| Function call | `writeln(double(21))` | Used as an expression. |
| Value parameter | `procedure show(n: integer);` | The callee receives an independent value. |
| Reference parameter | `procedure fill(var n: integer);` | The callee shares caller storage and initialization. |
| Forward declaration | `function f(n: integer): integer; forward;` | Declare a signature before its body. |
| Nested routine | A routine declaration inside another routine. | Resolve names lexically through enclosing scopes. |
| Function result | `double := n * 2`, `exit(n * 2)` | Assign a typed result before completion. |

### Calls and local scope

Program-level procedures, functions, and global sections precede the main block and may
interleave. Value parameters support scalar, array, and record types; argument
counts and types must match, allowing integer-to-real value widening. Arguments evaluate
left-to-right. Zero-parameter declarations/calls may omit parentheses or use `()`.
Parameter groups use semicolons; names within a group use commas. Arity errors use
`E_ARGUMENT`. Parameters/locals share a scope and cannot shadow their own routine's
name; other routines' private locals are inaccessible.

Procedures may declare local variables, shadow globals, call earlier routines, and call
themselves recursively. Each call has fresh locals and initialization flags; changing a
value parameter does not change its caller's variable. Calls that may modify an active
global `for` control are rejected, including transitive writes. `break`/`continue`
cannot cross a routine boundary. Nested routines and `var` reference parameters are
supported. Forward declarations support calls to later bodies and mutual recursion.
Limits: 128 routines, 64 parameters and 256 variables (including parameters) per
routine, and 64 simultaneous calls. Excessive call depth fails with located
`R_CALL_DEPTH`.

### Function results

Functions return scalar, array, or record values. Assign the function's name to set its
private result. Reading that name inside the function reads the result; use `name()` for
a zero-argument recursive call. Outside the function, a bare zero-argument name calls
it. Function calls must be used as expressions, and result/argument types must match,
allowing integer-to-real value widening. Results start uninitialized on every call;
completion without assigning a result produces `R_RESULT` at the function header.
Reading a result before assigning it produces `R_UNINITIALIZED`. The implicit result
counts toward the routine's 256-variable limit. Functions share the procedure scope,
value-parameter, recursion, and global-control protections. There is no `Result` alias
or `return` keyword. Discarding a function call as a statement uses `E_TYPE`. Calls in
arguments, conditions, bounds, and other expressions retain the same transitive
counted-control checks.

### Early return

Bare `exit` leaves the current procedure/function or finishes the main program. Set the
function result first (`f:=value; exit`); leaving it unset still produces `R_RESULT`.
Exiting nested counted loops invalidates all their control variables in that routine,
while caller loops are unaffected. In a function, `exit(expression)` evaluates once,
assigns the declared result, and performs the same cleanup. For example, `if n=0 then
exit(1)` returns an integer. Wrong result types and value exits in main/procedures fail
with `E_TYPE`; empty `exit()` fails with `E_SYNTAX`. A value exit cannot assign an
implicit result that is an active `for` control (`E_FOR_CONTROL`).

### Forward declarations and mutual recursion

Forward example: `function iseven(n:integer):boolean; forward;` registers a signature
before other routines use it. Supply exactly one later full-header body with the same
kind, parameter types/order/count, value/reference modes, and result type. Aliases and
underlying scalar types are compatible; parameter names may differ. Missing/conflicting
definitions fail with `E_FORWARD`; duplicate forwards/bodies with `E_DUPLICATE`. Calls
before any declaration still fail with `E_UNKNOWN`. The body must be in the same lexical
scope. A forward's parameter names are not visible elsewhere. Counted-loop protection
includes writes through mutual-recursion cycles, and the 64-call limit still applies.
Unresolved forwards locate the declaration; result failures locate the definition
header. Routine limits count unique identities, not both headers. Use named aggregate
types when repeating signatures, because constructors have nominal identity.

### Reference parameters

Reference groups: `procedure fill(var x:integer; value:integer);`. A `var` actual must
be a writable variable, field, element, or current function result of the same type. It
shares values/initialization, including duplicate or forwarded references; it may start
uninitialized for output-style procedures. Evaluate indexes once left-to-right.
Literals, constants, operators, and returned temporary fields/elements fail with
`E_LVALUE`. Arrays/records can be referenced without copies. Parentheses preserve
writable paths; unary signs/operators do not. Actual paths are evaluated without reading
their values. Wrong types use `E_TYPE`.

Passing a known active `for` control is rejected (`E_FOR_CONTROL`). Hidden aliases are
checked at runtime (`R_FOR_CONTROL`), including whole aggregate writes and var bindings
containing an active control. An integer reference used as a counter shares invalidation
with its caller after the loop. Returns and loop transfers clean up their protection
while retaining the caller's active controls. These reference-counter cases
intentionally differ from FPC, which rejects that syntax.

### Nested routines and captures

Nested procedures/functions may interleave with local const/type/var sections. They see lexically
enclosing declarations, earlier siblings, and same-scope forwards. Nearest declarations
shadow ancestors, even between routine/value/type names; collisions in one scope are
invalid. Inner names do not leak outside their parent.

Captured locals, parameters/references, aggregates, and function results belong to the
correct activation through recursion. A child can assign/read its parent's function name
as a result; use parentheses to call that function instead. There are no escaping
routine values. Exit and loop transfers remain local to the current routine. Enclosing
active counters retain runtime alias protection.

## Arrays and records

| Feature | Declaration or use | Notes |
| --- | --- | --- |
| Fixed array | `type Row = array[-2..2] of integer;` | Bounds are integer constant expressions. |
| Nested array | `type Matrix = array[1..3] of Row;` | Select with `m[i][j]`. |
| Record | `type Pair = record x, y: integer; ready: boolean end;` | Select with `p.x`. |
| Nested field | `p.rows[i].ready` | Field and index selectors compose. |
| Value copy | `b := a` | Same aggregate type; copied storage is independent. |
| Returned value | `make().x`, `make()[i]` | Temporary result selectors are read-only. |

### Fixed arrays

Bounds are checked integer constant expressions; negative/nonzero lower bounds are
supported. Declare global/local values and access them with `a[i]` or nested `m[i][j]`.
Indexes are integers and checked at runtime (`R_BOUNDS`); reversed bounds fail with
`E_ARRAY_RANGE`, wrong/nonconstant bounds with `E_TYPE`/`E_CONST`, and oversized storage
with `E_LAYOUT`, including unused named types. Comma-separated dimensions/indexes are
unsupported; use nested constructors/selectors.

Array aliases share identity; separately constructed array types are distinct. `var
a,b:array[1..2] of integer` shares one constructed type, whereas separate constructors
do not. Index failures locate the opening `[`, before address computation; combined
selector/expression depth is bounded. Whole/subarray copies require the same type,
preserve partial initialization, and produce independent values. For example, `b:=a`
copies initialized elements and keeps other leaves uninitialized. Copies may start
entirely uninitialized, use overlap-safe semantics, and check only selected scalar
leaves on later reads. Destination indexes evaluate once before the source; nested
indexes evaluate left-to-right. `readln(a[i])` accepts integer elements. Arrays cannot
be printed or compared as a whole. Array parameters and function results use value
copies, as do records.

### Records and aggregate routine values

Fields can contain supported types, including arrays or records; select with `p.x` or
`p.rows[i].ready`. Fields have a separate case-insensitive namespace; duplicates fail
with `E_DUPLICATE`, unknown fields with `E_FIELD`; selecting a field on a nonrecord uses
`E_TYPE`. Each record has at most 4,096 fields. Empty records are invalid; a final field
semicolon is optional. Record assignment requires the same type identity and copies
independent values, including each leaf's initialization state. Integer fields support
`readln`.

Array/record arguments are snapshotted left-to-right before later arguments run; a
callee's parameter writes do not change the caller. Functions return copied aggregates
using whole/field/element assignments or `exit(value)`. Partly initialized results are
allowed; reading an unwritten leaf still fails. A result with no write fails with
`R_RESULT`. Returned values support read-only selectors (`make().x`, `make()[i]`).
Temporary buffers share the 128 KiB activation budget and are released after
calls/consumption; memory exhaustion is located `R_MEMORY`. Whole aggregate
comparison/output remain unsupported.

## Strings and characters

| Operation | Example | Unit / behavior |
| --- | --- | --- |
| Capacity | `var s: string[20];` | UTF-8 bytes; capacity is 0 through 1,048,576. |
| Concatenation | `s := 'hé' + '😀';` | Three scalars occupying seven bytes. |
| Length | `length(s)` | Unicode scalars, not bytes or visual glyphs. |
| Comparison | `s < t`, `s = t` | Scalar lexicographic order; no normalization or case folding. |
| Index | `s[1]` | One-based Unicode scalar access. |
| Update | `s[2] := '😀'` | Replace one scalar, checking byte capacity. |
| Slice | `copy(s, 2, 3)` | Independent string of three scalars from position 2. |
| Scalar construction | `chr(128512)` | Character `😀`; invalid scalar values fail. |
| Scalar value | `ord(c)` | Integer scalar value. |

### Bounded UTF-8 strings

Capacities are integer constant expressions; `string[0]` holds only `''`. Variables
require initialization. Quoted literals are string expressions, and doubled quotes
escape quotes. `length(s)` counts Unicode scalars, including combining marks separately.
Comparisons `= <> < <= > >=` use scalar lexicographic order, without case folding or
normalization. Numeric/string conversion is unsupported.

Copies and value arguments/results are independent. Different capacities are allowed by
value when the actual UTF-8 bytes fit; overflow fails with located `R_STRING_CAPACITY`,
without truncation. Reference arguments require matching capacities. Strings work inside
arrays/records, captures, recursion, and routine results. Whole aggregate copies
preserve uninitialized string leaves.

`readln(s)` consumes the entire line, including leading/trailing spaces and empty lines.
Invalid Unicode fails with `R_ENCODING`; capacity is checked before writing.
`writeln(s)` uses the same text-only output and byte limits. For example, `var
s:string[2]; begin s:='é'; writeln(s) end` succeeds; assigning `'€'` fails. Equal string
capacities share canonical identity, even across independently written types/aliases.
Operand and value-argument snapshots precede later alias-changing calls. NUL and BOM
inside strings are preserved.

Constants support concatenation, length, and comparisons; invalid declared capacities or
folded values above 1 MiB use `E_STRING_CAPACITY`. All decoded/folded text shares an 8
MiB compiler budget (`E_LIMIT`). Materialized literals use the 256 KiB data region
(`E_LAYOUT`). Temporary values use the existing 128 KiB frame arena; large declared
frames or expressions can fail with `R_MEMORY`.

### Unicode characters, indexing, and slicing

`char` is one Unicode scalar (U+0000 through U+10FFFF, excluding surrogates).
`c:=chr(128512); writeln(c,ord(c));` prints `😀128512`. Quoted one-scalar literals and
named constants convert to char where a char value is required. Other string expressions
need indexing. Chars widen to strings for value assignment, `+`, and comparisons with
strings in either operand order, including empty strings. Mixed comparisons use the
same Unicode lexical order as string/string comparisons; integers/Booleans remain
distinct. Char/char comparisons are ordinal, with all six relations; char is not a
numeric arithmetic type, index, counted control, or case selector. Invalid chr values
fail with `R_CHARACTER` (or `E_CHARACTER` in constants).

`s[1]` selects the first Unicode scalar. `s[2]:='😀'` replaces one scalar and checks
UTF-8 capacity before writing. It preserves all other characters; it never edits an
arbitrary byte. Combining marks count separately. Invalid positions use `R_BOUNDS`.
Writable selections can be passed to `var char` parameters; aliases observe the captured
scalar position after intervening string changes and recheck bounds on each use.
Temporary results/literals allow read-only indexing. Base/index evaluate once before RHS
effects, then the captured position is rechecked against any changed container.
Uninitialized strings cannot provide views; parenthesized storage retains writability.

`copy(s,2,3)` returns an independent slice of three scalars starting at position 2.
Start must be 1..length+1, count nonnegative and within remaining characters;
`copy(s,length(s)+1,0)` is an empty end slice. Bounds do not clamp. Snapshot the source,
then evaluate start/count once left-to-right. Constants support indexing/slicing with
`E_BOUNDS`. Character writes/input retain initialization, capacity, encoding, and output
checks. `writeln(char)` emits one UTF-8 scalar, including NUL. Character constants
support chr/ord, comparisons, char-to-string concatenation, indexing, and copy under the
same text budgets.

## Typed pointers

```pascal
type
  PNode = ^Node;
  Node = record
    value: integer;
    next: PNode
  end;
```

| Operation | Example | Behavior |
| --- | --- | --- |
| Allocate | `new(p)` | Create an uninitialized pointee and assign its pointer. |
| Dereference | `p^.value := 1` | Access pointee storage with lifetime/bounds checks. |
| Empty pointer | `p := nil` | `nil` converts to any pointer value type. |
| Compare | `p = q`, `p <> nil` | Compare tokens without dereferencing them. |
| Dispose | `dispose(p)` | Release the object and set `p` to `nil`; aliases become stale. |

Unknown pointer targets can be declared later in the same type section. Other
forward/self types remain invalid. Visible targets bind immediately; later shadowing
does not rebind them. Unresolved pointer targets use `E_TYPE_FORWARD` at the originating
name; non-type targets use `E_TYPE`, and unknown by-value names use `E_UNKNOWN`. Cycles
must cross pointer edges; all pointee layouts, including anonymous/unused types, must be
finite (`E_LAYOUT`). Pointer interning/resolution is bounded as documented in the ABI
reference. Pointers to the same resolved target are compatible; distinct record/array
targets remain distinct. Nil converts to any pointer value type. Only assignment,
parameters/results, and `=`/`<>` are allowed; there is no address-of, pointer
arithmetic/cast, ordering, or pointer text I/O.

`p^.value:=1; p^.next:=nil;` initializes the fields of a new object. A new handle is
assigned only after allocation succeeds. Initialize links explicitly and dispose owned
objects explicitly; overwriting a pointer does not free its old object. Dereferencing
nil fails with `R_NIL`; released/reused aliases fail with `R_DANGLING`;
invalid/nil/duplicate disposal fails with `R_DISPOSE`. Allocation/growth limits use
`R_MEMORY`. Equality compares tokens without dereferencing stale pointers.

Dereferences can select fields/elements/strings, bind var parameters, and appear on
returned pointer values, e.g. `fill(first()^.value)`. Pointees stay borrowed through an
expression or var call, preventing disposal while a destination/ref/ character view is
live (`R_LIFETIME`). Value snapshots release their borrows before later arguments.
Active counted controls retain `R_FOR_CONTROL` protection. No reference escapes its
caller and no local address becomes a pointer: source pointers originate from `new`.
Pointer aliases alone do not borrow. Conditions release their borrows before the body;
value snapshots release them before later arguments, while reference borrows last until
return. Counter protection takes precedence over `R_LIFETIME`. Returned-pointer pointees
can be writable assignment/input/new/dispose targets; other temporaries stay read-only.

Heap allocation and generation checks are private generated Wasm code, with no external
allocator. Memory remains bounded to 64 MiB, with borrowed leases in the existing
activation arena. Simple whole-block reuse may fail under fragmentation; there is no
garbage collector. All behavior works in offline HTML exports.

## Checked increment

`inc(destination)` adds one to initialized writable integer storage, including fields,
array elements, references, and heap pointees. The destination path is evaluated once.
Overflow reports `R_OVERFLOW`; uninitialized values report `R_UNINITIALIZED`. Constants,
noninteger destinations, extra arguments, and writes to active counted controls are
rejected with the usual storage/type/control diagnostics. Hidden control aliases remain
checked at runtime. `inc` is reserved; no step argument is supported.

## Input and output

### Statement forms

| Form | Behavior |
| --- | --- |
| `write(a, b)` | Print arguments without implicit spaces or a trailing newline. |
| `writeln(a, b)` | Print arguments and append one LF. |
| `write;` | Produce no output. |
| `writeln;` | Print one LF. |
| `read(a, b)` | Read left-to-right, preserving remaining input. Numeric fields may cross line boundaries. |
| `readln(a, b)` | Read left-to-right within the current line, then discard its remainder and delimiter once. |
| `readln;` | Discard the remaining current line. |

Output accepts integer, real, Boolean, character, and string expressions. Booleans print
as `TRUE`/`FALSE`. Arrays, records, and pointers cannot be printed as whole values.
Empty I/O parentheses and formatted output are unsupported.

Input destinations must be writable integer, real, character, or string storage,
including fields, elements, dereferences, and reference character views. Boolean,
pointer, and whole-aggregate input are unsupported.

Evaluate each destination once, left-to-right, immediately before its read. If a later
field fails, earlier assignments remain; the failing destination must not be partially
written. Counter guards, pointer borrows, initialization, and character-view
bounds/capacity checks apply as for assignment. Bare `read` and empty parentheses use
`E_SYNTAX`; invalid input destination types use `E_TYPE`.

### Input rules by type

| Type | Parsing | `read` | `readln` |
| --- | --- | --- | --- |
| Integer | Signed decimal; require space/tab or line end after the token. | Skip spaces, tabs, and line boundaries; preserve the trailing delimiter. | Skip spaces/tabs within this line; discard the remainder after all fields. |
| Real | Signed decimal/exponent; require a token delimiter. | Skip spaces, tabs, and line boundaries; preserve the trailing delimiter. | Stay within this line; discard the remainder after all fields. |
| Character | One Unicode scalar, including leading whitespace. | Preserve spaces and consume an LF/CRLF delimiter as one LF scalar. | Read a scalar from this line; reject an empty line. |
| String | All remaining text on the current line, preserving spaces and empty lines. | Leave the line delimiter unread. | Discard the delimiter after all fields. |

LF and CRLF delimit lines. A final nonempty line need not end with a newline; a trailing
newline creates no additional line. Empty or malformed numeric input fails, as do
exhausted input and out-of-range values. Invalid Unicode uses `R_ENCODING`; string
capacity is checked before writing. `read(string)` at a present line delimiter returns
`''` without advancing. After fully consuming an unterminated final line, further value
reads use `R_INPUT_EOF`, but bare `readln` may still discard that retained line.

Real input uses the decimal/exponent rules above. Overflow or nonzero input rounding to
zero uses `R_INPUT_RANGE`; malformed input uses `R_INPUT_INVALID`.

### Multiple fields and line boundaries

For this statement, enter **both numbers on one line**, such as `2 3`:

```pascal
write('Insert two numbers: ');
readln(n, m);
writeln('Sum: ', n + m)
```

| Goal | Statements | Input example |
| --- | --- | --- |
| Two numbers from one line | `readln(n, m)` | `2 3` |
| One number from each of two lines | `readln(n); readln(m)` | First line `2`, second line `3` |
| Two numbers across whitespace or lines | `read(n, m)` | `2 3` or separate lines |
| Read a number and keep the rest as text | `read(n); readln(tail)` | `7 rest` gives `n = 7`, `tail = ' rest'`. |

Batch and interactive providers use the same parsing rules. Batch input is supplied
before Run; interactive input arrives as submitted console lines.

## Example playground console

These controls belong to the example browser integration and exported HTML.

| Control / action | Behavior |
| --- | --- |
| **Interactive console** | Default input mode; display output and submitted input in one transcript. |
| **Run** | Start a fresh execution and input/output state. |
| **Waiting for input** | Type at the caret directly after the prompt. |
| Enter | Submit one line, including an empty line. |
| Backspace/Delete, arrows, Home/End | Edit the active input; previous transcript text stays read-only. |
| Paste | Insert plain text; line breaks become spaces; do not submit automatically. |
| IME confirmation | Confirm composition without submitting the line. |
| **End input** | Close input; a subsequent read without available data reports `R_INPUT_EOF`. |
| **Stop** | Cancel computing or waiting. |
| **Batch input** | Supply complete input before Run; no JSPI requirement. |

Prompts from `write` appear before waiting, even without a newline. Interactive input
requires built-in browser JSPI; choose batch input when it is unavailable. Input waits
do not count toward the cumulative execution budget, and resuming does not reset it.
Input echo uses the input allowance rather than the program output allowance. Downloaded
runnable HTML supports both modes offline via `file://` in a compatible browser. This is
a line console, without ANSI terminal commands or raw keypress input.

## Storage, limits, and diagnostics

Variables and initialization flags use bounded addressable program storage. Each routine
call gets a fresh frame; normal and early returns release it. The frame arena is 128 KiB
within the 64 MiB program-memory limit, so large routines may produce located `R_MEMORY`
before the 64-call depth limit. Value arguments remain independent, and globals/locals
retain their scope semantics.

| Resource | Limit / rule |
| --- | --- |
| Source | 256 KiB |
| Input | 1 MiB of UTF-8, including interactive submissions |
| Program output | 1 MiB of UTF-8; input echo is accounted separately against input |
| Program memory | 64 MiB total |
| Activation frames and temporaries | 128 KiB within the total memory limit |
| Routine declarations | 128 |
| Parameters per routine | 64 |
| Variables per routine | 256, including parameters and the implicit function result |
| Simultaneous routine calls | 64; excess uses `R_CALL_DEPTH` |
| Labels/ranges per `case` | 1,024; a range counts as one |
| String capacity / constant value | At most 1 MiB |
| Compiler constant-text budget | 8 MiB |
| Real literals | At most 768 digits; exponent magnitude at most 4,096 |
| Browser compilation / active execution | Five seconds each; human input waits are excluded |

### Common diagnostics

| Code | Meaning |
| --- | --- |
| `E_UNSUPPORTED` | Encounter an unsupported construct, character, or operator. |
| `R_UNINITIALIZED` | Read storage before assigning it. |
| `R_RESULT` | Complete a function without setting its result. |
| `R_BOUNDS` | Invalid array index, string position, or slice bounds. |
| `R_MEMORY` | Exhaust available storage or a memory sub-budget. |
| `R_INPUT_INVALID`, `R_INPUT_RANGE`, `R_INPUT_EOF` | Malformed, out-of-range, or exhausted input. |
| `R_STRING_CAPACITY`, `R_ENCODING` | Exceed UTF-8 string capacity or supply invalid Unicode. |
| `E_FOR_CONTROL`, `R_FOR_CONTROL` | Modify or alias an active counted-loop control illegally. |
| `R_NIL`, `R_DANGLING`, `R_DISPOSE`, `R_LIFETIME` | Invalid pointer dereference, disposal, or borrow lifetime. |

Diagnostics include source locations where applicable. Columns count UTF-8 bytes, not
displayed characters. Compile-time errors use `E_` codes; runtime failures use `R_`
codes.

Compiler declaration counts, expression/selector nesting, text storage, and type
resolution also have bounded complexity. Exceeding a compiler work budget uses
`E_LIMIT`; invalid or oversized layouts use `E_LAYOUT`.

## Unsupported features

| Area | Not supported |
| --- | --- |
| General control transfer | `goto` and labels |
| Dynamic types | Dynamic/open arrays and other unlisted dialect-specific types |
| Routine values | Escaping closures or first-class routine values |
| Function syntax | `Result` alias and `return` keyword |
| Pointer operations | Address-of, pointer arithmetic/casts, ordering, and text I/O |
| Whole aggregates | Comparison and text output |
| Numeric/string conversion | Implicit or built-in numeric-to-string/string-to-numeric conversion |
| Terminal protocols | ANSI terminal commands and raw keypress input |

Reserved built-ins include `trunc`, `round`, `length`, `char`, `chr`, `ord`, `copy`,
`nil`, `new`, and `dispose`; `record` is also reserved. Rename earlier identifiers that
use these names.

## Formal grammar

This EBNF describes shipped syntax. Quoted strings are tokens; brackets denote optional
syntax, braces repetition, and `|` alternatives. `empty` consumes no token and is legal
wherever a statement is expected. Whitespace/comments can separate tokens; keywords
match case-insensitively. Literal spelling, declaration visibility, writable-storage
requirements, type checks, and other constraints are defined in the sections above
rather than repeated as grammar rules.

```text
program     = "program" identifier ";" declarations compound "." EOF ;
declarations = {constants | types | variables | routine} ;
constants   = "const" identifier "=" expression ";" {identifier "=" expression ";"} ;
types       = "type" identifier "=" type ";" {identifier "=" type ";"} ;
variables   = "var" declaration {declaration} ;
declaration = identifier {"," identifier} ":" type ";" ;
routine     = ("procedure" identifier [parameters]
              | "function" identifier [parameters] ":" type)
              ";" ("forward" ";" | declarations compound ";") ;
parameters  = "(" [parameter_group {";" parameter_group}] ")" ;
parameter_group = ["var"] declaration_group ;
declaration_group = identifier {"," identifier} ":" type ;
call        = identifier ["(" [expression {"," expression}] ")"] ;
type        = "^" type | "integer" | "boolean" | "real" | "char" | "string" ["[" expression "]"] | identifier
            | "array" "[" expression ".." expression "]" "of" type
            | "record" declaration_group {";" declaration_group} [";"] "end" ;
lvalue      = identifier {selector} | call selector {selector} ;
storage     = factor ; (* must resolve to writable storage *)
selector    = "[" expression "]" | "." identifier | "^" ;
compound    = "begin" statement {";" statement} "end" ;
statement   = assignment | compound | conditional | loop | repeat_loop | for_loop
            | selection | "break" | "continue" | "exit" ["(" expression ")"]
            | call | output | input | allocation | increment | empty ;
assignment  = lvalue ":=" expression ;
conditional = "if" expression "then" statement ["else" statement] ;
loop        = "while" expression "do" statement ;
repeat_loop = "repeat" statement {";" statement} "until" expression ;
for_loop    = "for" identifier ":=" expression ("to" | "downto") expression
              "do" statement ;
selection   = "case" expression "of" arm {";" arm} [";"]
              ["else" statement {";" statement}] "end" ;
arm         = label {"," label} ":" statement ;
label       = endpoint [".." endpoint] ;
endpoint    = ["+" | "-"] (integer_literal | identifier) | "true" | "false" ;
output      = ("write" | "writeln") ["(" argument {"," argument} ")"] ;
argument    = expression ;
input       = "read" "(" storage {"," storage} ")"
            | "readln" ["(" storage {"," storage} ")"] ;
allocation  = ("new" | "dispose") "(" storage ")" ;
increment   = "inc" "(" storage ")" ;
expression  = simple [relation simple] ;
simple      = ["+" | "-"] term {("+" | "-" | "or") term} ;
term        = factor {("*" | "/" | "div" | "mod" | "and") factor} ;
factor      = primary {selector} ;
primary     = integer_literal | real_literal | text_literal | "true" | "false" | "nil" | call | identifier
            | ("real" | "trunc" | "round" | "length" | "chr" | "ord") "(" expression ")"
            | "copy" "(" expression "," expression "," expression ")"
            | "(" expression ")" | "not" factor ;
relation    = "=" | "<>" | "<" | "<=" | ">" | ">=" ;
identifier  = (ASCII_letter | "_") {ASCII_letter | ASCII_digit | "_"} ;
```
