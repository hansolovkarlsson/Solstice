program TestOperators;
var
    a, b, neg_val : integer;
    res_lte, res_gte, res_neq : boolean;
    bool1, bool2, bool_res : boolean;
begin
    { --- 1. Unary Operations --- }
    a := 15;
    neg_val := -a;          { Unary minus: neg_val = -15 }
    writeln(neg_val);

    bool1 := true;
    bool2 := not bool1;     { Unary not: bool2 = false }
    writeln(bool2);

    { --- 2. Relational Operators (<=, >=, <>) --- }
    b := 20;
    res_lte := a <= b;      { 15 <= 20 -> true }
    res_gte := a >= 20;     { 15 >= 20 -> false }
    res_neq := a <> b;      { 15 <> 20 -> true }

    writeln(res_lte);
    writeln(res_gte);
    writeln(res_neq);

    { --- 3. Boolean Logic (and, or) --- }
    bool_res := bool1 and (a < b);   { true and true -> true }
    writeln(bool_res);

    bool_res := bool2 or (a > b);    { false or false -> false }
    writeln(bool_res);

    { --- 4. Precedence & Complex Expressions --- }
    { Demonstrates that 'not' and comparisons bind correctly in combined logic }
    bool_res := not (a + neg_val <> 0) and (b >= 20); { not false and true -> true }
    writeln(bool_res);
end.

