program TestWithNested;
type
    TPoint = record n: integer; end;
var
    a, b: TPoint;

procedure shadowTest;
var
    n: integer;
begin
    n := 999;
    with a do begin
        n := 42;
        writeln('inside with, n = ', n);
    end;
    writeln('outside with, local n = ', n);
end;

begin
    a.n := 1;
    b.n := 100;
    with a do begin
        n := 5;
        inc(n);
        writeln('a.n = ', n);
        with b do begin
            n := 50;
            writeln('b.n = ', n);
        end;
        writeln('back to a.n = ', n);
    end;
    shadowTest;
    writeln('a.n = ', a.n);
end.
