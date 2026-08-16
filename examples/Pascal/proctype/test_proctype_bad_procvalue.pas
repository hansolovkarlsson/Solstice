program TestProctypeBadProcvalue;
type
    TIntProc = procedure(x: integer);
var
    p: TIntProc;

procedure Foo(x: integer);
begin
end;

begin
    p := Foo;
    { Expected: Compile Error - a procedure-typed value can't be used
      in an expression }
    writeln(p(5));
end.
