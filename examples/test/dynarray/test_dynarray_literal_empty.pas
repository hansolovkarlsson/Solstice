program TestDynArrayLiteralEmpty;
var
    arr: array of integer;
begin
    arr := [];
    writeln(Length(arr));  { 0 }
    writeln(arr = nil);    { TRUE }
end.
