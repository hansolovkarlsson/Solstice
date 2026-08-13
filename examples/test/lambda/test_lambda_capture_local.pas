program TestLambdaCaptureLocal;

{ A lambda references an enclosing procedure's ordinary (non-static)
  local - must be rejected by reject_lambda_captures(), not silently
  allowed and not a crash. }

type
    TF = function: integer;

procedure Outer;
    var
        total: integer;
        f: TF;
begin
    total := 5;
    f := function: integer begin exit(total); end;
    writeln(f());
end;

begin
    Outer;
end.
