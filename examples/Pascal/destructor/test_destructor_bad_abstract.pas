program TestDestructorBadAbstract;
type
    TThing = class
    public
        destructor Destroy; abstract;
    end;
begin
end.
