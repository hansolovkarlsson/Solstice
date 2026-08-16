program TestCMBadInheritedCM;
type
    TBase = class
    public
        procedure Foo;
    end;
    TSub = class(TBase)
    public
        class procedure Bar;
    end;

procedure TBase.Foo;
begin
end;

procedure TSub.Bar;
begin
    inherited Foo;
end;

begin
end.
