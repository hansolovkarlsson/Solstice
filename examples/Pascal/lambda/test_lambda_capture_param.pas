program TestLambdaCaptureParam;

{ A lambda references an enclosing procedure's ordinary parameter - must
  be rejected, same as an ordinary local (see
  test_lambda_capture_local.pas). This is exactly the 'MakeAdder(n)'
  idiom explicitly cut from v1. }

type
    TF = function(x: integer): integer;

function MakeAdder(n: integer): TF;
begin
    MakeAdder := function(x: integer): integer begin exit(x + n); end;
end;

var
    add5: TF;

begin
    add5 := MakeAdder(5);
    writeln(add5(10));
end.
