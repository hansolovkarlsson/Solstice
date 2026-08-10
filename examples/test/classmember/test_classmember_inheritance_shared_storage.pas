program TestCMInheritShared;
type
    TBase = class
    public
        class var X: integer;
        class function Describe: integer;
    end;
    TSub = class(TBase)
    public
    end;

function TBase.Describe;
begin
    Describe := X;
end;

begin
    TBase.X := 42;
    writeln('TSub.X = ', TSub.X);
    TSub.X := 99;
    writeln('TBase.X = ', TBase.X);
    writeln('TSub.Describe = ', TSub.Describe);
end.
