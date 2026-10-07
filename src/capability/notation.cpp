// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "notation.h"

namespace capability {

    // One rule, one name; repeating the name adds another assertion to it. `=> !msg` means the code
    // must fail with msg in stderr; x = any container, k = a map key (the name stays short, the
    // assertions carry the rest). Everything here is asserted by test/run.sh.
    // A '(' in a name costs constraints: no ':' (brief_entry() would strip it to a call head), and a
    // head without a space must match [a-z][a-z0-9_]* (check.py's structural pass).
    const form s_notation[] = {
        {"x[null]=size", R"EX(return [1,2,3][null]; => 3)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=size", R"EX(return "hi"[null]; => 2)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=size", R"EX(var l = lst[1,2,3]; return l[null]; => 3)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=size", R"EX(return map{"a":1,"b":2}[null]; => 2)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=size", R"EX(var o=map{"a":[1,2]}; return o.a[null]; => 2)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=push", R"EX(var v=[1]; v[null]=2; return v; => [1,2])EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=push", R"EX(var l = lst[1,2,3]; l[null]=4; return l[null]; => 4)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=refused", R"EX(var s="hi"; s[null]="x"; => ![TypeError] string does not support index write)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=refused", R"EX(var m=map{}; m[null]="x"; => ![TypeError] map key must be a string)EX", nullptr, "", clan_notation | clan_index},
        {"x[null]=refused", R"EX(var o=map{"n":5}; return o.n[null]; => ![TypeError] type does not support [null] index)EX", nullptr, "", clan_notation | clan_index},
        {"x[null] terminal only", R"EX(var v=[1,2]; return v[null].x; => ![IndexError] null index only allowed as terminal)EX", nullptr, "", clan_notation | clan_index},
        {"x[-1]=back", R"EX(return [1,2,3][-1]; => 3)EX", nullptr, "", clan_notation | clan_index},
        {"x[-1]=back", R"EX(return "hello"[-1]; => "o")EX", nullptr, "", clan_notation | clan_index},
        {"x[-1]=back", R"EX(return "hello"[-2]; => ![IndexError] string index out of range)EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b]=slice", R"EX(return [1,2,3,4,5][1,3]; => [2,3])EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b]=slice", R"EX(return "hello"[1,3]; => "el")EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b]=slice", R"EX(var l = lst[1,2,3,4]; return l[1,3]; => [2,3])EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b]=slice", R"EX(return [1,2,3,4,5][2,null]; => [3,4,5])EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b]=slice", R"EX(return [1,2,3,4,5][1,5,2]; => [2,4])EX", nullptr, "", clan_notation | clan_index},
        {"x[a,b] read-only", R"EX(var v=[1,2,3]; v[0,1]=9; => ![TypeError] slice is read-only)EX", nullptr, "", clan_notation | clan_index},
        {"[v:N]=fill", R"EX(return [0:5]; => [0,0,0,0,0])EX", nullptr, "", clan_notation | clan_index},
        {"[v:N]=fill", R"EX(return [1,2,3:3]; => [1,2,3,3,3])EX", nullptr, "", clan_notation | clan_index},
        {"[v:N]=fill", R"EX(return vec[:3]; => [null,null,null])EX", nullptr, "", clan_notation | clan_index},
        {"delete x", R"EX(var x=5; delete x; return x; => ![NameError] Undefined)EX", nullptr, "", clan_notation},
        {"delete x", R"EX(delete zz; return 1; => 1)EX", nullptr, "", clan_notation}, // an unknown name deletes silently
        {"delete x[i]", R"EX(var v=[1,2]; delete v[0]; return v; => [2])EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var m=map{"a":1}; delete m["a"]; return m; => {})EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var o=map{"s":map{"t":1}}; delete o.s.t; return o; => {"s":{}})EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var m=map{"a":1}; return delete m["a"]; => true)EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var m=map{"a":1}; return delete m["z"]; => false)EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var v=[1,2]; delete v[null]; => ![ConvError] Cannot delete with null index)EX", nullptr, "", clan_notation | clan_index},
        {"delete x[i]", R"EX(var s="abc"; delete s[0]; => ![TypeError] Type does not support delete)EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i]", R"EX(var o=map{"s":"hi"}; return o.s[-1]; => "i")EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i]", R"EX(var i=0; var o=map{"s":"hi"}; return o.s[i]; => !index in dot chain must be a literal)EX", nullptr, "", clan_notation | clan_index},
        // The tail-tolerant read is the one silent half of chain access; mid-chain and [] fail loudly.
        {"x.k[i].x: only tail absent: read null, write insert",
         R"EX(var o=map{"v":[map{"x":1}]}; return o.v[0].x; => 1)EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i].x: only tail absent: read null, write insert",
         R"EX(var o=map{"a":map{}}; return o.a.nn; => null)EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i].x: only tail absent: read null, write insert",
         R"EX(var o=map{"a":map{}}; o.a.nn=5; return o; => {"a":{"nn":5}})EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i].x: only tail absent: read null, write insert",
         R"EX(var o=map{}; return o.zz.x; => ![KeyError] map key not found: zz)EX", nullptr, "", clan_notation | clan_index},
        {"x.k[i].x: only tail absent: read null, write insert",
         R"EX(var o=map{"a":1}; return o["zz"]; => ![KeyError] map key not found: zz)EX", nullptr, "", clan_notation | clan_index},
        // lang.md §15.2: only a bare variable passes by reference; any other argument is a copy.
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(var o=map{"a":[1,2]}; $vec_push(o.a,3); return o; => {"a":[1,2]})EX", nullptr, "",
         clan_notation},
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(var o=map{"a":[1,2]}; $vec_insert(o.a,0,9); return o; => {"a":[1,2]})EX", nullptr, "",
         clan_notation},
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(var v=[1,2]; $vec_push(v,3); return v; => [1,2,3])EX", nullptr, "", clan_notation},
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(var m=map{"a":[1,2]}; m["a"][null]=3; return m; => {"a":[1,2,3]})EX", nullptr, "",
         clan_notation},
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(var v=[1,2]; var w=v; $vec_push(w,3); return v; => [1,2])EX", nullptr, "", clan_notation},
        {"$vec_push: o.a is a copy, write lost -- in-place caps need a bare variable",
         R"EX(return $vec_reverse([1,2]); => null)EX", nullptr, "", clan_notation},
        {"x[k]:string", R"EX(var m=map{}; m[1]=2; => ![TypeError] map key must be a string)EX", nullptr, "", clan_notation | clan_index},
        {"x[k]:string", R"EX(return map{"a":1}[0]; => ![TypeError] map key must be a string)EX", nullptr, "", clan_notation | clan_index},
        {"x[k]=v writes", R"EX(var m=map{}; m["a"]=1; return m; => {"a":1})EX", nullptr, "", clan_notation | clan_index},
        {"x[i]=v overwrites", R"EX(var v=[1,2]; v[0]=9; return v; => [9,2])EX", nullptr, "", clan_notation | clan_index},
        {"x+y: same kind", R"EX(return "a" + "b"; => "ab")EX", nullptr, "", clan_notation},
        {"x+y: same kind", R"EX(return [1,2] + [3]; => [1,2,3])EX", nullptr, "", clan_notation},
        {"x+y: same kind", R"EX(return map{"a":1} + map{"b":2}; => {"a":1,"b":2})EX", nullptr, "", clan_notation},
        {"x+y: same kind", R"EX(return 1 + 2.0; => 3.0)EX", nullptr, "", clan_notation},
        {"x+y: same kind", R"EX(return "n=" + 5; => ![TypeError] Expected numbers, strings, arrays, lists or maps for +)EX", nullptr, "", clan_notation},
        // printf-style specs are not it: `%s` loses the % (measured); the convention is Qt-style %1 %2
        {"fmt = Qt-style %1 %2", R"EX(return string("%2-%1", "a", "b"); => "b-a")EX", nullptr, "", clan_notation},
        {"fmt = Qt-style %1 %2", R"EX(return string("%s", "x"); => "s")EX", nullptr, "", clan_notation},
        {"fmt = Qt-style %1 %2", R"EX(return string("%1%%", "a"); => "a%")EX", nullptr, "", clan_notation},
        {"map{} literal", R"EX(return map{"a":1}["a"]; => 1)EX", nullptr, "", clan_notation},
        {"map{} literal", R"EX(return map{}[null]; => 0)EX", nullptr, "", clan_notation},
        {"map{} literal", R"EX(return map{1:2}; => ![TypeError] map key must be a string)EX", nullptr, "", clan_notation},
        {"map{} literal", R"EX(return {a:1}; => !Unexpected token)EX", nullptr, "", clan_notation},
        {"map{} literal", R"EX(return map{a:1}; => ![NameError] Undefined: a)EX", nullptr, "", clan_notation},
        // Facts a task kept getting wrong, each measured: they belong to the resident list (the
        // model writes its first draft from it), not to prose behind a query.
        {"string(int,n)=base · string(real,n)=places",
         R"EX(return string(3, 2) + " " + string(3.0, 2); => "0b11 3.00")EX", nullptr, "", clan_notation},
        {"string for int/double/bool, $json_str for containers",
         R"EX(return string(map{"a":1}); => ![ConvError] Cannot convert to string)EX", nullptr, "",
         clan_notation},
        {"string for int/double/bool, $json_str for containers",
         R"EX(return $json_str(map{"a":1}); => "{\n\t\"a\": 1\n}")EX", nullptr, "", clan_notation},
        {"$vec_sort: nested left alone",
         R"EX(var v=[[3],[1,2]]; $vec_sort(v); return v; => [[3],[1,2]])EX", nullptr, "", clan_notation},
        {"$regex_match: the whole string",
         R"EX(return [$regex_match("b", "abc"), $regex_find("b", "abc")]; => [false,"b"])EX", nullptr, "",
         clan_notation},
        // No JSON form for inf/nan, and the client reads an int past 2^53 as a double -- string() out.
        {"inf/nan->null, int>2^53->float: use string", R"EX(return inf; => null)EX", nullptr, "",
         clan_notation},
        {"inf/nan->null, int>2^53->float: use string",
         R"EX(return string(1e308 + 1e308); => "inf")EX", nullptr, "", clan_notation},
        {"inf/nan->null, int>2^53->float: use string",
         R"EX(return [string(9007199254740993), 9007199254740993 == 9007199254740992]; => ["9007199254740993",false])EX",
         nullptr, "", clan_notation},
        // Byte-wise by design: `string` is unencoded bytes (GBK/hex ride it too), so chars need vec().
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return [$str_size("中"), $str_size("中a"), $str_size("中文"[0])]; => [3,4,1])EX",
         nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return "中文"[null]; => 6)EX", nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return [$regex_find_all(".", "中")[null], $regex_find_all("[。！？]", "。")[null]]; => [3,3])EX",
         nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return $str_size($str_left("中文", 2)); => 2)EX", nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return [vec("中a"), string([20013,74]), $str_size(string([20013,74]))]; => [[20013,97],"中J",4])EX",
         nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(return vec(string(bytes("e4b8","hex"))); => ![ConvError] Invalid UTF-8 sequence in string)EX",
         nullptr, "", clan_notation | clan_index},
        {"byte-wise $str_*, [], regex; chars: vec(s)",
         R"EX(var g=string(bytes("你好","gbk")); return $str_size(string(bytes(g),"gbk")); => 6)EX",
         nullptr, "", clan_notation | clan_index},
    // A wrapped value passes for a real one, so overflow throws (ALXRUN_OVERFLOW_CHECK).
#if ALXRUN_OVERFLOW_CHECK
        {"int overflow: throws, never wraps",
         R"EX(return 9223372036854775807 + 1; => ![OverflowError] Integer addition overflow)EX", nullptr, "",
         clan_notation},
        {"int overflow: throws, never wraps",
         R"EX(return 9223372036854775807 * 2; => ![OverflowError] Integer multiplication overflow)EX",
         nullptr, "", clan_notation},
#endif
    };

    const table& notation_table() {
        static const table t = {"notation", s_notation, sizeof(s_notation) / sizeof(s_notation[0])};
        return t;
    }

} // namespace capability
