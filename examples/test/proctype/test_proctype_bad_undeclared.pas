program TestProctypeBadUndeclared;
type
    TIntProc = procedure(x: integer);
var
    p: TIntProc;
{ Expected: Compile Error - 'Bogus' is undeclared }
begin
    p := Bogus;
end.
