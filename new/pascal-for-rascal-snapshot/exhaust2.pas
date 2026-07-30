program Exhaust2;
var i: integer; s: string; unused: string;
begin
    for i := 1 to 260 do begin
        s := 'x';
        unused := s + 'y';   { each concatenation with a different context creates distinct entries via readln-free means is tricky purely at compile time since literals dedup - use readln instead }
    end;
end.
