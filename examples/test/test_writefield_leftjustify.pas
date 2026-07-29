program TestWriteFieldLeftJustify;
var
    price: real;
    i: integer;
    s: string;
    b: boolean;
    w: integer;
begin
    i := 42;
    writeln('[', i:-5, ']');            { [42   ] }
    writeln('[', i:5, ']');             { [   42] - right-justify unaffected }

    price := 19.9;
    writeln('[', price:-10:2, ']');     { [19.90     ] }

    s := 'hi';
    writeln('[', s:-6, ']');            { [hi    ] }

    b := true;
    writeln('[', b:-8, ']');            { [TRUE    ] }

    { negative width as a runtime expression, not just a literal }
    w := -7;
    writeln('[', i:w, ']');             { [42     ] }
end.
