program TestDynArrayLiteralArgBadElem;
function Sum(a: array of integer): integer;
begin
    Sum := 0;
end;
begin
    writeln(Sum([1, 'two', 3]));
end.
