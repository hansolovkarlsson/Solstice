program TestDynArrayLiteralWiden;
var
    arr: array of real;
begin
    { integer literals into a 'real' array - each element must widen. }
    arr := [1, 2, 3];
    writeln(Length(arr));      { 3 }
    write(arr[0]:0:1, ' ', arr[1]:0:1, ' ', arr[2]:0:1);
    writeln;                    { 1.0 2.0 3.0 }
end.
