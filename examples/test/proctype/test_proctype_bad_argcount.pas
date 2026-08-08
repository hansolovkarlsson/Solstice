program TestProctypeBadArgcount;
type
    TIntProc = procedure(x: integer);
var
    p: TIntProc;

procedure Foo(x: integer);
begin
end;

begin
    p := Foo;
    { Expected: Compile Error - wrong argument count }
    p(1, 2);
end.
