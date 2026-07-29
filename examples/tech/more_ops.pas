program TestMathLogic;
var
    rem, quot : integer;
    x_res1, x_res2 : boolean;
begin
    rem := 17 mod 5;      { rem = 2 }
    quot := 17 div 5;     { quot = 3 }
    writeln(rem);
    writeln(quot);

    x_res1 := true xor false; { true }
    x_res2 := true xor true;  { false }
    writeln(x_res1);
    writeln(x_res2);
end.