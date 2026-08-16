program TestLambdaArgument;

{ Pass a lambda literal directly as an argument to a functional/
  procedural parameter, no intermediate variable. Apply(sq, 5) = 25. }

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(function(n: integer): integer begin exit(n * n); end, 5));
end.
