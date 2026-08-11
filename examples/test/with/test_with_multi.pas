program TestWithMulti;
type
    TPoint = record n: integer; end;
var
    a, b: TPoint;
begin
    a.n := 1;
    b.n := 100;

    { nested form }
    with a do with b do begin
        n := 5;
        inc(n);
        writeln('nested: n = ', n);
    end;
    writeln('nested: a.n = ', a.n, ' b.n = ', b.n);

    a.n := 1;
    b.n := 100;

    { comma-list form - should behave identically to the nested form above }
    with a, b do begin
        n := 5;
        inc(n);
        writeln('multi: n = ', n);
    end;
    writeln('multi: a.n = ', a.n, ' b.n = ', b.n);
end.
