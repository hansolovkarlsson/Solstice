program TestClassArrayfieldBadIdx;
type
    TBuffer = class
        data: array[0..3] of integer;
    end;
var
    b: TBuffer;
begin
    new(b);
    { 'data' used bare, no '[i]' - Expected: Compile Error. }
    writeln(b.data);
    dispose(b);
end.
