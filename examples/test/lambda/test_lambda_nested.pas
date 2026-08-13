program TestLambdaNested;

{ A lambda literal passed as an argument from inside another lambda's
  own body - recursion through parse_lambda_literal() itself. The outer
  lambda calls Apply with an inner lambda that squares its argument.
  Apply(sq, 6) = 36. }

type
    TOuter = function(v: integer): integer;

var
    outer: TOuter;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    outer := function(v: integer): integer
    begin
        exit(Apply(function(n: integer): integer begin exit(n * n); end, v));
    end;
    writeln(outer(6));
end.
