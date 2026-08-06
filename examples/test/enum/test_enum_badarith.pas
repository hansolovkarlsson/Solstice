program TestEnumBadArith;
type
    TColor = (Red, Green, Blue);
var
    c, d: TColor;
begin
    c := Red;
    d := c * 2;
end.
