program TestDynArrayReturnRecursive;
var
    a: array of integer;

{ Builds [1, 2, ..., n] recursively - exercises both an explicit
  recursive self-call ('Build(n - 1)') and reading/indexing/SetLength'ing
  the function's own return value in the same body, without either
  interpretation being confused for the other. }
function Build(n: integer): array of integer;
var
    sub: array of integer;
    i: integer;
begin
    if n <= 0 then begin
        SetLength(Build, 0);
    end else begin
        sub := Build(n - 1);
        SetLength(Build, Length(sub) + 1);
        for i := 0 to High(sub) do
            Build[i] := sub[i];
        Build[High(Build)] := n;
    end;
end;

begin
    a := Build(3);
    writeln(Length(a));         { 3 }
    writeln(a[0], ' ', a[1], ' ', a[2]);  { 1 2 3 }
end.
