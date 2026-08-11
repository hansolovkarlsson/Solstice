program TestDestructorLocalVarParam;
type
    TThing = class
    public
        destructor Destroy;
    end;
var t: TThing;

destructor TThing.Destroy;
begin
    writeln('destroying');
end;

procedure DisposeIt(var t: TThing);
begin
    dispose(t);
end;

procedure Local;
var t: TThing;
begin
    new(t);
    dispose(t);
end;

begin
    Local;
    new(t);
    DisposeIt(t);
end.
