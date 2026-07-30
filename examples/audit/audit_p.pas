program AuditP;
procedure test;
var arr: array[1..4] of integer; i: integer;
begin
    for i := low(arr) to high(arr) do
        arr[i] := i;
    writeln(length(arr));
end;
begin
    test;
end.
