program TestDestructorPrivate;
type
    TThing = class
    private
        destructor Destroy;
    end;
var t: TThing;

destructor TThing.Destroy;
begin
    writeln('private destroy runs via dispose');
end;

begin
    new(t);
    dispose(t);
end.
