program TestIsasBadLeft;
type
    TFoo = class
    public
        x: integer;
    end;
var
    n: integer;
begin
    n := 5;
    writeln(n is TFoo);
end.
