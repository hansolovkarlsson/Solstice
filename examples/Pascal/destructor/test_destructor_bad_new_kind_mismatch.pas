program TestDestructorBadNewKind;
type
    TBase = class
    public
        procedure Cleanup;
    end;
    TSub = class(TBase)
    public
        destructor Cleanup;
    end;
begin
end.
