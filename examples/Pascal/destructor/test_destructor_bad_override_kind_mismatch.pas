program TestDestructorBadOverrideKind;
type
    TBase = class
    public
        destructor Destroy;
    end;
    TSub = class(TBase)
    public
        procedure Destroy;
    end;
begin
end.
