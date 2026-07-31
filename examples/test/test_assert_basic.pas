program TestAssertBasic;
var
    x: integer;
begin
    x := 5;
    assert(x > 0);
    writeln('ok so far');
    assert(x > 10, 'x must exceed 10');
end.
