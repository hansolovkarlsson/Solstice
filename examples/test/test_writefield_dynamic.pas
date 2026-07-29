program TestWriteFieldDynamic;
var w, p, i: integer; x: real;
begin
    w := 8;
    p := 3;
    x := 2.71828;
    write(x:w:p);
    writeln;

    { width as an expression, not just a variable }
    for i := 1 to 3 do
        writeln(i:i+2);
end.
