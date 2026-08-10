program TestIsasBadUnknown;
type
    TFoo = class
    public
        x: integer;
    end;
var
    f: TFoo;
begin
    new(f);
    writeln(f is NoSuchType);
    dispose(f);
end.
