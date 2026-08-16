program TestDynArrayCopyBadCount;
var
    a, b: array of integer;
begin
    SetLength(a, 3);
    b := Copy(a, 0, 'x');
end.
