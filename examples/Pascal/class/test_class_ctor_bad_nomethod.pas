program TestClassCtorBadNomethod;
type
    TFoo = class
        x: integer;
    end;
var
    f: TFoo;
begin
    { TFoo has no method named 'NoSuchMethod'. Expected: Compile Error. }
    new(f, NoSuchMethod());
end.
