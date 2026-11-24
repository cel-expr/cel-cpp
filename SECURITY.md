# Security Policy

This document describes the security model, threat boundaries, and security
recommendations for `cel-cpp`. It is intended to help developers understand what
security guarantees `cel-cpp` provides and how to safely parse, type-check, and
evaluate CEL expressions and configurations.

The structure and tiering of this threat model are modeled after the
[Protocol Buffers Security Policy](https://github.com/protocolbuffers/protobuf/blob/main/SECURITY.md).

--------------------------------------------------------------------------------

## Reporting a Vulnerability

If you believe you have discovered a security vulnerability in `cel-cpp`, please
report it via
[Google's official vulnerability disclosure channel](https://bughunters.google.com/report).

For reports that do not meet the criteria or bar for the Google Vulnerability
Reward Program (VRP) but are still security-sensitive, you can privately report
them by opening a
[draft GitHub Security Advisory](https://github.com/cel-expr/cel-cpp/security/advisories/new).

If an issue relates to something listed under **Outside of Threat Model
(Best-Effort Hardened)** below, please open a
[public GitHub issue](https://github.com/cel-expr/cel-cpp/issues) instead.

--------------------------------------------------------------------------------

## Best Practices

*   **Apply and verify defensive limits:** `cel-cpp` enforces default limits in
    the parser, type checker, and runtime, but several important protections
    require explicit caller configuration:
    *   **Parser (`cel::ParserOptions`):** Keep default limits enabled or lower
        them for untrusted input (`max_recursion_depth = 32`,
        `expression_size_codepoint_limit = 100000`,
        `expression_node_limit = 100000`, `error_recovery_limit = 12`,
        `error_recovery_token_lookahead_limit = 512`).
    *   **Type Checker (`cel::CheckerOptions`):** Keep
        `max_expression_node_count` (default `100000`), `max_error_issues`
        (default `20`), and `max_type_decl_nesting` (default `13`) bounded. If
        type declarations themselves are not fully trusted, lower
        `max_type_decl_nesting` (for example, to `3`) to prevent compounding
        type nesting.
    *   **Runtime (`cel::RuntimeOptions`):**
        *   Keep `comprehension_max_iterations` bounded (default `10000`; `0`
            disables the limit).
        *   **Set `regex_max_program_size` to a non-zero value** if untrusted
            expressions or inputs can supply regular expressions. By default,
            `regex_max_program_size` is `0` (unbounded).
        *   Keep `enable_comprehension_list_append` and
            `enable_comprehension_mutable_map` disabled unless evaluating
            trusted, type-checked ASTs.
    *   **Canonical Extensions (`extensions/`):** Only enable the extensions
        and extension versions required by the application, and tighten
        extension-specific limits for untrusted expressions—for example, lower
        `cel::extensions::ListsExtensionOptions::max_range_size` (default
        `1000000`), bound
        `cel::extensions::StringsExtensionOptions::max_precision` (default
        `1000`), and set `RuntimeOptions::regex_max_program_size` when enabling
        the regex extension.
    *   **AST Validators (`validator/`):** Use
        `ComprehensionNestingLimitValidator`, `BindNestingLimitValidator`,
        `AstDepthValidator`, and `RegexValidator` to bound expression complexity
        and prevent CPU/memory amplification before evaluation.
    *   **Input and environment bounds:** Bound the size and nesting of values
        passed in `cel::Activation` (strings, bytes, lists, maps, and Protobuf
        messages). `cel-cpp` does not enforce a global memory cap or runtime
        cost budget on string/list concatenation (`+`), `cel.bind` chaining, or
        comprehension output growth.
*   **Treat YAML configurations as code:** Environment YAML
    (`cel::EnvConfigFromYaml`) and policy YAML (`cel::YamlPolicyParser`) should
    only be loaded from trusted or vetted sources, similarly to source code or
    `.proto` schemas. When loading environment YAML configurations shared with
    `cel-go` or `cel-java`, enable
    `cel::EnvConfigFromYamlOptions::strict = true` so that unimplemented fields
    (such as `limits`, `validators`, `imports`, and `features`) fail closed
    instead of being silently ignored.
*   **Only trust ASTs from trusted compilation pipelines:** Prefer compiling
    expressions with `cel-cpp`, `cel-go`, or `cel-java` in a trusted pipeline
    rather than accepting arbitrary serialized `ParsedExpr` or `CheckedExpr`
    Protobufs from untrusted callers. Maintain a mechanism to recompile stored
    ASTs in case a compiler bug is discovered or runtime invariants tighten.
*   **Audit custom extensions and functions:** Any custom function or extension
    registered in the runtime executes directly in the host C++ process. Callers
    are responsible for the memory safety, termination, and resource bounds of
    custom functions.

--------------------------------------------------------------------------------

## Threat Model and Security Boundaries

Different surfaces of `cel-cpp` operate under different trust assumptions.
Following the
[Protocol Buffers Security Policy](https://github.com/protocolbuffers/protobuf/blob/main/SECURITY.md),
our threat model divides surfaces into three tiers:

### Proactively Hardened

These surfaces are actively hardened against adversarial inputs with structural
defenses and continuous fuzzing.

*   Security vulnerabilities identified in these paths are treated with high
    priority and are actively fixed.
*   We avoid breaking changes whenever possible, but may introduce them when
    strictly necessary to close a security issue.

### Reactively Hardened

Defensive hardening is applied to these surfaces, but with weaker guarantees
compared to proactively hardened surfaces.

*   Serious security issues (such as memory corruption or RCE) are prioritized
    for fixing.
*   We generally will not break backward compatibility guarantees to address
    lower-severity issues on these surfaces outside of major updates.
*   When exposing these surfaces to potentially malicious inputs, callers should
    apply application-level isolation (such as sandboxing) or additional
    validation.

### Outside of Threat Model (Best-Effort Hardened)

These surfaces are expected to be used with trusted inputs or are inherently
sensitive to caller configuration.

*   We maintain reasonable levels of defensive hardening where feasible so that
    sensible inputs do not crash, and we welcome bug reports and pull requests
    via [public GitHub issues](https://github.com/cel-expr/cel-cpp/issues).
*   However, crashes, `CHECK` failures, or resource exhaustion (DoS) reachable
    only through malicious inputs on these surfaces are not treated as security
    vulnerabilities.

--------------------------------------------------------------------------------

## Proactively Hardened

### Parsing and Type Checking Single Expressions (Standard Library)

Parsing (`cel::Parser`) and type checking (`cel::TypeChecker`) single CEL
expressions configured with the standard library are designed to handle
arbitrary, untrusted expression text, assuming defensive limits
(`cel::ParserOptions` and `cel::CheckerOptions`) are applied.

*   **Structural defenses:** The parser enforces recursion depth, codepoint
    length, AST node count, and error recovery limits. The type checker uses
    iterative AST traversal and enforces expression node count, error count, and
    type nesting limits.
*   **Fuzzing:** Lexing, parsing, and type checking are continuously fuzzed.
*   **Scope:** Memory safety bugs (such as out-of-bounds reads/writes,
    use-after-free, or uncontrolled stack recursion) and crashes on arbitrary
    expression text with default limits are in scope for security reports.

### Runtime Evaluation of Trusted Checked ASTs (Standard Library)

Planning and evaluating ASTs that have been parsed and type-checked by a trusted
compilation pipeline (`cel-cpp`, `cel-go`, or `cel-java`) using the standard
library is proactively hardened against memory corruption and uncontrolled stack
recursion.

*   **Structural defenses:** Both AST traversal during planning and program
    evaluation use iterative heap-based execution. The runtime is continuously
    fuzzed.
*   **Caveat on stored ASTs:** Because we cannot guarantee the absence of
    historical bugs in any compiler implementation, any pipeline that persists
    and reuses `CheckedExpr` ASTs should maintain a mechanism to recompile
    expressions from source.
*   **Caveat on predictable runtime / DoS:** Type checking guarantees type
    soundness, not bounded CPU or memory usage. Achieving predictable runtime
    guarantees requires careful environment design and caller-configured limits
    (see [Resource Consumption and Denial of
    Service](#resource-consumption-and-denial-of-service)).

--------------------------------------------------------------------------------

## Reactively Hardened

### Runtime Evaluation of Unchecked or Arbitrary ASTs

The runtime is somewhat hardened against arbitrary or unchecked ASTs (for
example, parse-only ASTs or `ParsedExpr`/`CheckedExpr` Protobufs deserialized
from untrusted sources), but it is **not safe against maliciously constructed
ASTs**:

*   **Optimization invariants:** `cel::CreateOptimizedRuntimeBuilder` (and
    `cel::RuntimeOptions::enable_comprehension_list_append` /
    `enable_comprehension_mutable_map`) relies on AST invariants established by
    standard macros and type checking. Hand-rolled or maliciously crafted ASTs
    that violate these invariants are unsafe with these options enabled.
*   **Comprehension vulnerability check:**
    `cel::EnableComprehensionVulnerabiltyCheck` provides partial
    defense-in-depth for custom or hand-written comprehension ASTs, but it is
    opt-in, non-exhaustive, and not intended for deeply nested ASTs.
*   **Unbounded AST shapes:** Externally deserialized ASTs bypass parser depth
    and node limits; deeply nested arbitrary ASTs may cause stack overflow
    during C++ object destruction or excessive resource usage during planning.

Applications that must accept serialized ASTs from untrusted sources should
recompile from CEL source text instead, or validate and sandbox evaluation.

### CEL Policies (Trusted / Semi-Trusted Authors)

The CEL Policy compiler (`policy/`) composes multiple CEL expressions and
variables into a single program. While individual expressions are parsed and
type-checked, policy YAML parsing and multi-rule composition are not hardened
against adversarial policy authors.

### Canonical CEL Extensions

The canonical CEL extensions (such as `lists`, `strings`, `regex`, `sets`,
`bindings`, `encoders`, `math`, `protos`, `optional`, and
`two-var-comprehensions`) offer more complex functions and macros than the
standard library, making them inherently more expensive to evaluate and more
susceptible to resource exhaustion. Enable only the extensions and versions
needed, and apply extension-specific limits and AST validation for untrusted
expressions:

*   **Lists (`cel::extensions::ListsCompilerLibrary`):** Lower
    `ListsExtensionOptions::max_range_size` from its default of `1000000` (for
    example, to `100`) to prevent `lists.range(n)` from allocating large lists,
    and bound input list sizes before operations such as `flatten`, `distinct`,
    `sort`, and `sortBy`.
*   **Strings (`cel::extensions::StringsCompilerLibrary`):** Configure
    `StringsExtensionOptions::max_precision` (default `1000`) for `format()`,
    and bound input sizes for `replace`, `split`, and `join` to limit memory
    amplification.
*   **Regex (`cel::extensions::RegexExtCompilerLibrary`):** Set
    `RuntimeOptions::regex_max_program_size` to a non-zero value and validate
    static patterns with `RegexValidator`. Be aware that `regex.replace` (with
    capture-group backreferences) and `regex.extractAll` can amplify output size
    relative to the input string.
*   **Bindings (`cel::extensions::BindingsCompilerLibrary`):** Chained
    `cel.bind` expressions combined with concatenation (`+`) can produce
    exponential string or list growth in linear AST size (for example, repeated
    doubling). Use `BindNestingLimitValidator` (and `AstDepthValidator`) to
    restrict nesting when exposing `cel.bind` to untrusted expressions.
*   **Sets and Two-Variable Comprehensions (`sets`, `two-var-comprehensions`):**
    Bound input collection sizes (set operations can be quadratic in list
    length) and keep `comprehension_max_iterations` and
    `ComprehensionNestingLimitValidator` enabled.

--------------------------------------------------------------------------------

## Outside of Threat Model (Best-Effort Hardened)

### YAML Environment and Policy Parsing

Parsing CEL environment configurations (`cel::EnvConfigFromYaml`) and CEL
policies (`cel::YamlPolicyParser`) assumes trusted (or vetted, semi-trusted)
authors:

*   **Underlying parser:** `cel-cpp` delegates YAML parsing to `yaml-cpp`, a
    non-memory-safe C++ library without a formal untrusted-input security model.
    While `cel-cpp` applies defensive post-parse validation where practical
    (such as duplicate-key, nesting-depth, and node-count checks in
    `EnvConfigFromYaml`), we make no security guarantees against malicious YAML
    inputs (including YAML alias/anchor amplification or parser bugs in
    `yaml-cpp`).
*   **Environment expressiveness:** Independent of YAML parsing bugs, a CEL
    environment configuration can define types, variables, and function
    overloads that make type checking or runtime evaluation unpredictably
    expensive (for example, deeply compounding generic type declarations).
*   **Cross-implementation field parity:** `EnvConfigFromYaml` does not
    currently implement the `imports`, `limits`, `validators`, or `features`
    fields supported by other CEL implementations. Callers loading
    configurations authored for `cel-go` or `cel-java` should set
    `EnvConfigFromYamlOptions::strict = true` (or inspect `warnings`) to avoid
    silently ignoring safety limits.

### Untrusted Declarations and Protobuf Descriptors

Loading untrusted `google::protobuf::DescriptorPool` definitions or untrusted
custom type/function declarations is outside the threat model. Protobuf
descriptors are treated as trusted schemas (consistent with the
[Protocol Buffers Security Policy](https://github.com/protocolbuffers/protobuf/blob/main/SECURITY.md#dynamicmessage-on-untrusted-descriptors)).

### Experimental Features

Features explicitly documented as experimental or work-in-progress (for example,
`cel::ParserOptions::enable_pratt_parser`) are outside the threat model until
promoted to supported status.

### Adversarial Application Code and API Misuse

Violating C++ API contracts—such as passing invalid pointers, dangling
`absl::string_view`s, out-of-lifetime `google::protobuf::Arena` or
`DescriptorPool` references, or calling non-thread-safe builder APIs
concurrently—is an application integration error rather than a library
vulnerability.

### Differential Parsing and Evaluation

Evaluating or validating the same CEL expression (or YAML environment
configuration) with different `ParserOptions`, `CheckerOptions`,
`RuntimeOptions`, library or extension versions, schemas, or CEL implementations
(`cel-cpp`, `cel-go`, `cel-java`) can produce different parse trees, type-check
results, or runtime outcomes. For example, options such as
`enable_empty_wrapper_null_unboxing`, `enable_cross_numeric_comparisons`,
`enable_timestamp_duration_overflow_errors`, `use_json_field_names`, or
differing standard library and extension versions intentionally change
evaluation semantics.

This risk also applies to stored `CheckedExpr` ASTs. Compiling an expression
once avoids parser and type-checker skew, but evaluating the same stored AST
under different `RuntimeOptions`, `DescriptorPool` schemas, library versions, or
runtime implementations can still produce different results. Likewise, an AST
produced by a newer compiler or a different implementation may rely on AST
extensions, overload IDs, or features that an older or different runtime either
rejects or interprets differently.

Additionally, behaviors that are not explicitly defined by the CEL specification
have no guarantees of consistency across implementations or library versions.
This includes the exact structure of ASTs, non-result side effects of
evaluation, and specific configuration options. Callers cannot assume that the
compilers or runtimes in `cel-cpp`, `cel-go`, and `cel-java` are exact drop-in
replacements for one another.

An architecture where one service validates an expression or configuration and
forwards the raw text, or distributes a stored AST, to a downstream service that
re-parses or evaluates it under different options, schemas, library versions, or
implementations is outside the threat model.

For best security practice:

*   Compile once in a trusted pipeline when possible, and keep runtime options,
    `DescriptorPool` schemas, extension versions, and library versions aligned
    across services that evaluate the resulting ASTs (with a mechanism to
    recompile stored ASTs on version upgrades).
*   Enable `EnvConfigFromYamlOptions::strict = true` when loading YAML
    environment configurations shared across implementations so unsupported
    fields fail closed.
*   Enforce authorization decisions in the service performing the action rather
    than relying solely on upstream validation.
*   If a system evaluates expressions across multiple runtimes or library
    versions, the integrator is responsible for validating that behaviors are
    consistent for their use case.

### Inexact Enforcement of Configured Limits

Configured limits (such as `ParserOptions::max_recursion_depth`,
`ParserOptions::expression_node_limit`,
`CheckerOptions::max_expression_node_count`,
`CheckerOptions::max_type_decl_nesting`, and
`RuntimeOptions::comprehension_max_iterations`) are defensive bounds intended to
prevent uncontrolled recursion and resource exhaustion. They are not guaranteed
to map exactly to specific syntactic input or output features (for example,
macro expansion or internal AST structure may cause effective counts to vary by
a constant factor).

Edge cases that exceed a configured limit slightly without exposing
uncontrolled recursion or meaningful resource exhaustion are not considered
security vulnerabilities or bugs, though we welcome reports and pull requests
via [public GitHub issues](https://github.com/cel-expr/cel-cpp/issues) for
minor updates to make enforcement more consistent. However, cases where
recursion depth or resource consumption can scale non-linearly (or become
unbounded) with respect to a configured limit are in scope as security issues.

--------------------------------------------------------------------------------

## Resource Consumption and Denial of Service

Even for proactively hardened surfaces, CEL's non-Turing-complete design does
not by itself guarantee bounded memory or CPU time for all expressions and
inputs. Reports of CPU or memory exhaustion are **not** considered security
vulnerabilities when they stem from:

*   **Unconfigured optional limits:** For example, leaving
    `RuntimeOptions::regex_max_program_size = 0` (unbounded), leaving
    `ListsExtensionOptions::max_range_size` at its high default, disabling
    `comprehension_max_iterations`, or omitting
    `ComprehensionNestingLimitValidator`, `BindNestingLimitValidator`, or
    `AstDepthValidator` when evaluating untrusted expressions.
*   **Unbounded input or output growth:** Combining large `Activation` inputs
    with string concatenation (`+`), list concatenation (`+`), `cel.bind`
    chaining, or nested comprehensions without application-level input-size
    bounds or AST validators.
*   **Expensive custom functions or environments:** Custom functions,
    overloads, or extensions registered by the host application.

For latency- or memory-sensitive multi-tenant services evaluating untrusted
expressions, always combine `cel-cpp`'s parser, checker, runtime, and extension
limits with AST validators and application-level input-size caps.
