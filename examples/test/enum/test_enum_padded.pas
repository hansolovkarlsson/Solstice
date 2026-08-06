program TestEnumPadded;
type
    TColor = (Red, Green, Blue);
var
    c: TColor;
begin
    c := Green;
    writeln('[', c:10, ']');
end.
