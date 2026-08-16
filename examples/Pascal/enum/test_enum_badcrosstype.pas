program TestEnumBadCrossType;
type
    TColor = (Red, Green, Blue);
    TSize = (Small, Big);
var
    c: TColor;
    s: TSize;
begin
    if c = s then writeln('same');
end.
