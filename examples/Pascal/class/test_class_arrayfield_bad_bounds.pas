program TestClassArrayfieldBadBounds;
type
    TBuffer = class
        data: array[0..3] of integer;
    end;
var
    b: TBuffer;
    i: integer;
begin
    new(b);
    i := 10; { in range at compile time (a plain integer), but out of
               the array's own declared 0..3 bounds at runtime }
    b.data[i] := 1;
    dispose(b);
end.
