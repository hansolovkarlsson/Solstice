program TestDestructorRecordField;
type
    TThing = class
    public
        destructor Destroy;
    end;
    THolder = record
        item: TThing;
    end;
var h: THolder;

destructor TThing.Destroy;
begin
    writeln('destroying');
end;

begin
    new(h.item);
    dispose(h.item);
end.
