program TestDestructorExplicitCall;
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
    { an ordinary method call, independent of dispose() - the instance
      is still alive afterward, just its cleanup code already ran }
    t.Destroy;
    writeln('still alive (not disposed)');
end.
