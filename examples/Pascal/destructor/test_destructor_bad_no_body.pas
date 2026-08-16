program TestDestructorBadNoBody;
type
    TThing = class
    public
        destructor Destroy;
    end;
var t: TThing;
begin
    new(t);
    dispose(t);
end.
