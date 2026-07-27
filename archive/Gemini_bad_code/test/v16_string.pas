program TestWithStrings;
var
    x, y : integer;
    is_valid : boolean;
begin
    x := 10;
    y := 25;
    is_valid := y > x;

    writeln('--- Program Test Run ---');
    writeln('Value of X: ', x);
    writeln('Value of Y: ', y);
    writeln('Is Y > X? ', is_valid);
end.
