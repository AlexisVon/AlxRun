" Vim syntax file for Alexis Script (Core) (.axc)
" Language: Alexis Script (Core)
" Maintainer: alexis
" Based on: doc/scpt/lang.md (core engine, zero extensions)
"
" Priority rule: later definition wins at same start position.
" Order: delimiters < operators < keywords < numbers < comments < strings/chars.

if exists("b:current_syntax")
  finish
endif

" ============================================================================
" Delimiters (lowest priority — first in file)
" ============================================================================
syn match alexisDelimiter "[{}()\[\];,]"

" ============================================================================
" Operators — single-char first (lower pri), compound after (higher pri)
" ============================================================================
syn match alexisOperator "[+\-*/%&|^~<>=!?:@]"
syn match alexisOperator "<<=\|>>=\|<<\|>>\|++\|--\|&&\|||\|!=\|==\|<=\|>=\|+=\|-=\|\*=\|/=\|%=\|&=\||=\|\^="
syn match alexisDot      "\."
syn match alexisDotDot    "\.\."
syn match alexisColonColon "::"
syn match alexisAt        "@"

" ============================================================================
" Keywords
" ============================================================================
syn keyword alexisDeclaration var def
syn keyword alexisConditional  if else switch case default
syn keyword alexisRepeat       while for
syn keyword alexisBranch       break continue return
syn keyword alexisImport       import link as
syn keyword alexisException    try catch throw
syn keyword alexisDelete       delete

" ============================================================================
" Constants
" ============================================================================
syn keyword alexisBoolean true false
syn keyword alexisNull    null nan inf

" ============================================================================
" Builtin types — int/float/bool/string/bytes/vec/map/lst;
" the lookahead leaves name( to alexisBuiltin below
" ============================================================================
syn match   alexisType "\<\%(int\|float\|bool\|string\|bytes\|vec\|map\|lst\)\>\ze\s*\%((\)\@!"

" ============================================================================
" Builtin functions — same names, only match when followed by (
" ============================================================================
syn match alexisBuiltin "\<\(env\|type\|int\|float\|string\|bool\|bytes\|vec\|map\|lst\|here\|eval\|trap\)\>\ze\s*("

" ============================================================================
" Numbers — plain decimal first, then hex/oct/bin override, then float
" ============================================================================
syn match alexisInteger "\<[0-9][0-9_]*\>"
syn match alexisInteger "\<0[bB][01][01_]*\>"
syn match alexisInteger "\<0[oO][0-7][0-7_]*\>"
syn match alexisInteger "\<0[xX][0-9a-fA-F][0-9a-fA-F_]*\>"
syn match alexisFloat   "\<[0-9][0-9_]*\.[0-9][0-9_]*\([eE][+-]\=[0-9][0-9_]*\)\=[fF]\=\>"
syn match alexisFloat   "\<[0-9][0-9_]*[eE][+-]\=[0-9][0-9_]*[fF]\=\>"
syn match alexisFloat   "\(^\|\W\)\@<=\.[0-9][0-9_]*\([eE][+-]\=[0-9][0-9_]*\)\=[fF]\=\>"

" ============================================================================
" Line comment — syn REGION (overrides all matches, even operators with /)
" ============================================================================
syn keyword alexisTodo contained TODO FIXME XXX HACK NOTE
syn region  alexisLineComment start="//" end="$" contains=alexisTodo

" ============================================================================
" Block comment — syn region, naturally highest priority
" ============================================================================
syn region alexisBlockComment start="/\*" end="\*/" fold contains=alexisTodo,alexisBlockComment

" ============================================================================
" Strings — double-quoted, escape sequences inside
" ============================================================================
syn match  alexisEscape contained "\\[ntr\\"'0]"
syn match  alexisEscape contained "\\x[0-9a-fA-F]\{2}"
syn region alexisString  start=/"/ skip=/\\"/ end=/"/ contains=alexisEscape

" ============================================================================
" Char literals — single-quoted, single char or escape
" ============================================================================
syn match  alexisCharEscape contained "\\[ntr\\"'0]"
syn match  alexisCharEscape contained "\\x[0-9a-fA-F]\{2}"
syn region alexisChar start=/'/ end=/'/ contains=alexisCharEscape

" ============================================================================
" Backtick raw strings — no escapes, multiline
" ============================================================================
syn region alexisBacktickString start=/`/ end=/`/

" ============================================================================
" Highlight links
" ============================================================================
hi def link alexisLineComment   Comment
hi def link alexisBlockComment  Comment
hi def link alexisTodo          Todo
hi def link alexisString        String
hi def link alexisEscape        SpecialChar
hi def link alexisChar          Character
hi def link alexisCharEscape    SpecialChar
hi def link alexisFloat         Float
hi def link alexisInteger       Number
hi def link alexisDeclaration   StorageClass
hi def link alexisConditional   Conditional
hi def link alexisRepeat        Repeat
hi def link alexisBranch        Keyword
hi def link alexisImport        Include
hi def link alexisException     Exception
hi def link alexisDelete        Keyword
hi def link alexisBoolean       Boolean
hi def link alexisNull          Constant
hi def link alexisType          Type
hi def link alexisBuiltin       Function
hi def link alexisOperator      Operator
hi def link alexisDot           Operator
hi def link alexisAt            Operator
hi def link alexisDotDot        Operator
hi def link alexisColonColon    Operator
hi def link alexisDelimiter     Delimiter
hi def link alexisBacktickString String

let b:current_syntax = "alexis"
