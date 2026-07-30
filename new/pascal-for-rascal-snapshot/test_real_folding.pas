program TestRealFolding;
begin
    writeln(3.14 + 2.0);          { pure real+real fold }
    writeln(2 + 3.14);            { widening fold: int literal + real literal }
    writeln(3.14 * 2);            { widening on the other side }
    writeln(10.0 / 4.0);          { real division fold }
    writeln(-2.5);                { unary minus fold }
    writeln(abs(-3.5));           { abs fold }
    writeln(sqr(2.5));            { sqr fold }
    writeln(trunc(3.9));          { trunc fold }
    writeln(round(3.5));          { round fold }
    writeln(3.14 > 2.0);          { comparison fold }
end.
