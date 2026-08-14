program TestDynArrayLiteralByteRange;
var
    arr: array of byte;
begin
    arr := [1, 2, 300];   { 300 is out of range for byte (0..255) }
    writeln(arr[2]);
end.
