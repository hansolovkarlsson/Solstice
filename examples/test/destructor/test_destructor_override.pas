program TestDestructorOverride;
type
    TBase = class
    public
        destructor Destroy;
    end;
    TSub = class(TBase)
    public
        destructor Destroy;
    end;
var
    s: TSub;
    b: TBase;

destructor TBase.Destroy;
begin
    writeln('TBase.Destroy');
end;

destructor TSub.Destroy;
begin
    writeln('TSub.Destroy');
    inherited;
end;

begin
    new(s);
    { held via a BASE-typed variable - proves dispose() dispatches
      virtually through the RUNTIME tag, not TBase's own static slot }
    b := s;
    dispose(b);
end.
