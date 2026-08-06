program TestStaticBadSubrange;
type
    TAge = 0..150;

procedure bump;
var
    static a: TAge;
begin
    a := a + 200;
end;

begin
    bump;
end.
