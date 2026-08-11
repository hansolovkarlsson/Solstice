program TestDestructorBadComplexExpr;
type
    TThing = class
    public
        destructor Destroy;
    end;
    PHolder = ^THolder;
    THolder = record
        item: TThing;
    end;
var
    p: PHolder;
    t: TThing;

destructor TThing.Destroy;
begin
end;

begin
    new(p);
    new(t);
    p^.item := t;
    dispose(p^.item);
end.
