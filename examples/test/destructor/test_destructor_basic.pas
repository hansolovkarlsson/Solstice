program TestDestructorBasic;
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

begin
    new(t);
    writeln('before dispose');
    dispose(t);
    writeln('after dispose');
end.
