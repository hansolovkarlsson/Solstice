program TestIO;
var
    x, y : integer;
    is_greater : boolean;
begin
    writeln(100);

    { Read integer value directly from terminal }
    readln(x);
    
    y := 20;
    writeln(x + y);

    is_greater := y > x;
    writeln(is_greater);
end.