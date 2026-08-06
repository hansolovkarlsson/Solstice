program TestEnumBasic;
type
    TColor = (Red, Green, Blue);
var
    c, d: TColor;
begin
    c := Red;
    writeln('c = ', c);
    d := succ(c);
    writeln('d = succ(c) = ', d);
    writeln('pred(d) = ', pred(d));
    writeln('ord(c) = ', ord(c));
    writeln('ord(Blue) = ', ord(Blue));
    writeln('c = Red? ', c = Red);
    writeln('c < d? ', c < d);
    writeln('Red < Blue? ', Red < Blue);
end.
