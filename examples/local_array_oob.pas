program LocalArrayOob;
var i, x: integer;

procedure foo;
var arr: array[1..3] of integer;
begin
    i := 10;
    arr[i] := 1;
    x := arr[i];
    writeln(x);
end;

begin
    foo;
end.
