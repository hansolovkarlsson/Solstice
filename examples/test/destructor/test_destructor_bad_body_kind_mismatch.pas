program TestDestructorBadBodyKind;
type
    TThing = class
    public
        destructor Destroy;
    end;

procedure TThing.Destroy;
begin
end;

begin
end.
