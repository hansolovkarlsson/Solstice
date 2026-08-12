program TestSizedIntBasic;
var
    b: byte;
    s: shortint;
    w: word;
begin
    b := 200;
    s := -100;
    w := 40000;
    writeln(b, ' ', s, ' ', w);
    b := b + 10;
    s := s + 5;
    w := w + 1;
    writeln(b, ' ', s, ' ', w);
end.
