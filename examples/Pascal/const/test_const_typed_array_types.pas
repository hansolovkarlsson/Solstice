program TestConstTypedArrayTypes;
const
    { every scalar element type a typed constant array supports }
    Letters: array[1..3] of char = ('a', 'b', 'c');
    Flags: array[0..2] of boolean = (true, false, true);
    Fractions: array[1..2] of real = (3.14, 2.71);
var
    i: integer;
begin
    for i := 1 to 3 do write(Letters[i]);
    writeln;
    for i := 0 to 2 do write(Flags[i], ' ');
    writeln;
    writeln(Fractions[1]:0:2, ' ', Fractions[2]:0:2);
end.
