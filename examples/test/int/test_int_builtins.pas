program TestIntBuiltins;
var x, y: integer;
begin
    x := -7;
    writeln('abs(-7) = ', abs(x));
    writeln('sqr(-7) = ', sqr(x));
    writeln('odd(-7) = ', odd(x));
    writeln('odd(4) = ', odd(4));
    writeln('succ(9) = ', succ(9));
    writeln('pred(9) = ', pred(9));

    y := 10;
    inc(y);
    writeln('after inc(y): ', y);
    inc(y, 5);
    writeln('after inc(y,5): ', y);
    dec(y);
    writeln('after dec(y): ', y);
    dec(y, 3);
    writeln('after dec(y,3): ', y);
end.
