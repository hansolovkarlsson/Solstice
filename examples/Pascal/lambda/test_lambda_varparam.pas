program TestLambdaVarParam;

{ A lambda with a 'var' scalar parameter. Doubles x in place via the
  stored procedural value; x=21 -> 42. }

type
    TDoubler = procedure(var x: integer);

var
    d: TDoubler;
    x: integer;

begin
    d := procedure(var x: integer) begin x := x * 2; end;
    x := 21;
    d(x);
    writeln(x);
end.
