program TestDestructorBadCtorSugar;
type
    TThing = class
    public
        destructor Destroy;
    end;
var t: TThing;

destructor TThing.Destroy;
begin
end;

begin
    new(t, Destroy());
end.
