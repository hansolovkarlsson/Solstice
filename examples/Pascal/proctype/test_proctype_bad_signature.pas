program TestProctypeBadSignature;
type
    TIntProc = procedure(x: integer);
var
    p: TIntProc;

procedure Foo(x: real);
begin
end;

{ Expected: Compile Error - signature mismatch (real vs integer) }
begin
    p := Foo;
end.
