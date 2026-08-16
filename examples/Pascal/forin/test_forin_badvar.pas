program TestForInBadVar;
type
    TColor = (Red, Green, Blue);
var
    s: set of TColor;
    c: TColor;
begin
    s := [Red, Blue];
    for c in s do
        writeln(ord(c));
end.
