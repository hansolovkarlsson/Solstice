program TestDestructorBadSecond;
type
    TThing = class
    public
        destructor Destroy;
        destructor Cleanup;
    end;
begin
end.
