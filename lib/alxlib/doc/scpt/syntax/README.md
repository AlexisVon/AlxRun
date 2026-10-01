# Alexis Script Syntax Highlighting

This directory contains **core** syntax highlighting files for Alexis Script (.axc).

## Core vs Extended

- **Core** (`doc/scpt/syntax/`): Base engine with zero extensions. Only includes built-in functions (`env`, `type`, `int`, `float`, `string`, `bool`, `bytes`, `vec`, `map`, `lst`, `here`, `eval`, `trap`) and keywords.

- **Extended** (`example/Scpt/syntax/`): Includes host-registered `$xxx` extension functions (`$print`, `$input`, `$tojs`, `$fmjs`, `$exec`, `$fread`, `$fwrite`, `$datetime`, etc.). These are optional and provided by the host application.

## Files

### VSCode Extension

- `vscode/syntaxes/alexis.tmLanguage.json` - TextMate grammar
- `vscode/language-configuration.json` - Language configuration
- `vscode/package.json` - Extension manifest
- `vscode/snippets/axc.json` - Code snippets

### Vim Syntax

- `vim/syntax/alexis.vim` - Syntax highlighting
- `vim/ftdetect/alexis.vim` - Filetype detection

## Key Differences

| Feature | Core | Extended |
|---------|------|----------|
| `$xxx` extension functions | ❌ Not included | ✅ Included |
| `$` variable highlighting | ❌ Not included | ✅ Included |
| Built-in functions | ✅ All included | ✅ All included |
| Keywords | ✅ All included | ✅ All included |

## Extension Mechanism

The `$xxx` prefix is a lexical-level hard isolation mechanism. The base engine has **zero extensions** — any `$xxx` call will produce a compile error. Host applications register extensions via:

```cpp
// Register extension function
eng->set_extend("print", handler);

// Register predefined constant
eng->set_define("PI", 3.14159);
```

See `doc/scpt/lang.md` §15 for details on the extension mechanism.
