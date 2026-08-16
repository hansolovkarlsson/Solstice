program TestIsasBadUnrelated;
type
    TFoo = class
    public
        x: integer;
    end;
    TBar = class
    public
        y: integer;
    end;
var
    f: TFoo;
begin
    new(f);
    writeln(f is TBar);
    dispose(f);
end.
