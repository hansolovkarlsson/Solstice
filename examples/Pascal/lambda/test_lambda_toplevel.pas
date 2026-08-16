program TestLambdaTopLevel;

{ A lambda literal written directly in the main program body -
  nesting_depth starts at -1 there, the shallowest possible nesting
  case for parse_lambda_literal(). }

type
    TF = function(x: integer): integer;

var
    f: TF;

begin
    f := function(x: integer): integer begin exit(x + 1); end;
    writeln(f(9));
end.
