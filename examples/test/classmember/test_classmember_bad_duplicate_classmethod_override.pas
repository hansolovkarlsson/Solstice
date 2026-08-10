program TestCMBadDupOverride;
type
    TBase = class
    public
        class procedure Foo;
    end;
    TSub = class(TBase)
    public
        class procedure Foo;
    end;

procedure TBase.Foo;
begin
end;

procedure TSub.Foo;
begin
end;

begin
end.
