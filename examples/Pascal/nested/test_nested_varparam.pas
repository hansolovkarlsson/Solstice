program TestNestedVarParam;
var result: integer;

{ AddFive is an ordinary top-level procedure taking a 'var' parameter }
procedure AddFive(var n: integer);
begin
    n := n + 5;
end;

{ Outer's own 'var' parameter lives in ITS OWN frame; the nested
  procedure Bump forwards it on to AddFive as a 'var' argument too -
  exercises OP_PUSH_ENCLOSING_REF (the levels_up-aware generalization of
  OP_PUSH_LOCAL_REF) }
procedure Outer(var total: integer);
    procedure Bump;
    begin
        AddFive(total);
    end;
begin
    Bump;
    Bump;
end;

begin
    result := 1;
    Outer(result);
    writeln('result=', result);
end.
