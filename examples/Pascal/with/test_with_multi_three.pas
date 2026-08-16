program TestWithMultiThree;
type
    TA = record x: integer; end;
    TB = record y: integer; end;
    TC = record x: integer; end;   { shares field name 'x' with TA }
var
    a: TA;
    b: TB;
    c: TC;
begin
    a.x := 1;
    b.y := 2;
    c.x := 3;

    with a, b, c do begin
        writeln('x = ', x);   { c.x - last-listed target shadows a's same-named field }
        writeln('y = ', y);   { b.y - only source of y }
        x := 99;
    end;
    writeln('a.x = ', a.x, ' c.x = ', c.x);
end.
