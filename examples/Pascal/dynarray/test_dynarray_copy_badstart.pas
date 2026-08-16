program TestDynArrayCopyBadStart;
var
    a, b: array of integer;
begin
    SetLength(a, 3);
    b := Copy(a, 'x');
end.
