program TestIsasBadTarget;
type
    TFoo = class
    public
        x: integer;
    end;
    PPlain = ^integer;
var
    f: TFoo;
begin
    new(f);
    writeln(f is PPlain);
    dispose(f);
end.
