program TestDynArrayEmpty;
var
    arr: array of integer;
begin
    writeln(Length(arr));
    SetLength(arr, 4);
    writeln(Length(arr));
    SetLength(arr, 0);
    writeln(Length(arr));
    SetLength(arr, 3);
    writeln(Length(arr));
end.
