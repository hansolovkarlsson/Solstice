program TestClassInheritMultilevel;
type
    TA = class
        a: integer;
    end;
    TB = class(TA)
        b: integer;
    end;
    TC = class(TB)
        c: integer;
    end;
var
    obj: TC;
begin
    { fields from every ancestor, two levels up, all on one instance }
    new(obj);
    obj.a := 1;
    obj.b := 2;
    obj.c := 3;
    writeln(obj.a, ' ', obj.b, ' ', obj.c);
    dispose(obj);
end.
