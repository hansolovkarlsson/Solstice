program TestDynArrayCopyClamping;
var
    a, b, c, d, e: array of integer;
    i: integer;
begin
    SetLength(a, 3);
    for i := 0 to High(a) do
        a[i] := i + 1;         { 1 2 3 }

    b := Copy(a, 0, 100);       { count beyond available - clamps }
    writeln(Length(b));          { 3 }

    c := Copy(a, 10);            { start beyond length - empty, not an error }
    writeln(Length(c));          { 0 }
    writeln(c = nil);            { TRUE }

    d := Copy(a, -5, 2);         { negative start clamps to 0 }
    writeln(Length(d));          { 2 }
    write(d[0], ' ', d[1]);
    writeln;                      { 1 2 }

    e := Copy(a, 1, -1);         { negative count treated as "rest of array" }
    writeln(Length(e));          { 2 }
    write(e[0], ' ', e[1]);
    writeln;                      { 2 3 }
end.
