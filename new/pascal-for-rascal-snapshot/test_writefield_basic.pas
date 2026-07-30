program TestWriteFieldBasic;
var
    price: real;
    i: integer;
    s: string;
    b: boolean;
begin
    price := 19.9;
    writeln('[', price:0:2, ']');       { [19.90] }
    writeln('[', price:10:2, ']');      { [     19.90] }

    i := 42;
    writeln('[', i:5, ']');             { [   42] }
    writeln('[', i, ']');               { [42] - no width, unaffected }

    s := 'hi';
    writeln('[', s:6, ']');             { [    hi] }

    b := true;
    writeln('[', b:8, ']');             { [    TRUE] }

    { width alone on a real, no precision }
    writeln('[', price:10, ']');

    { multiple fields in one call }
    writeln(i:4, price:8:2, s:5);
end.
